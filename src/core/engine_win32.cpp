//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/engine.h"

#include <windows.h>

#include "core/error.h"
#include "core/format.h"
#include "core/guard.h"
#include "core/log.h"

namespace rcedit {

namespace {

bool IsNotFound( unsigned long code ) noexcept
{
    return code == ERROR_RESOURCE_TYPE_NOT_FOUND
        || code == ERROR_RESOURCE_NAME_NOT_FOUND
        || code == ERROR_RESOURCE_LANG_NOT_FOUND;
}

bool IsEnumerationEnd( unsigned long code ) noexcept
{
    return code == ERROR_RESOURCE_DATA_NOT_FOUND || IsNotFound( code )
        || code == ERROR_RESOURCE_ENUM_USER_STOP || code == ERROR_SUCCESS;
}

struct EnumContext
{
    HMODULE module = nullptr;
    std::vector< ResourceEntry >* entries = nullptr;
    std::error_code ec;
    ResourceId type;
    ResourceId name;
};

BOOL CALLBACK OnLanguage(
    HMODULE module,
    LPCWSTR type,
    LPCWSTR name,
    WORD lang,
    LONG_PTR param )
{
    auto* ctx = reinterpret_cast< EnumContext* >( param );

    HRSRC found = ::FindResourceExW( module, type, name, lang );
    if( found == nullptr ) {
        ctx->ec = LastWin32Error();
        return FALSE;
    }

    const DWORD size = ::SizeofResource( module, found );
    ctx->entries->push_back(
        ResourceEntry{ ResourceKey{ ctx->type, ctx->name, lang }, size } );
    return TRUE;
}

BOOL CALLBACK
OnName( HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param )
{
    auto* ctx = reinterpret_cast< EnumContext* >( param );
    ctx->name = FromLpcwstr( name );

    if( !::EnumResourceLanguagesW( module, type, name, OnLanguage, param ) ) {
        const DWORD code = ::GetLastError();
        if( !IsEnumerationEnd( code ) ) {
            ctx->ec = Win32Error( code );
            return FALSE;
        }
    }
    return ctx->ec ? FALSE : TRUE;
}

BOOL CALLBACK OnType( HMODULE module, LPWSTR type, LONG_PTR param )
{
    auto* ctx = reinterpret_cast< EnumContext* >( param );
    ctx->type = FromLpcwstr( type );

    if( !::EnumResourceNamesW( module, type, OnName, param ) ) {
        const DWORD code = ::GetLastError();
        if( !IsEnumerationEnd( code ) ) {
            ctx->ec = Win32Error( code );
            return FALSE;
        }
    }
    return ctx->ec ? FALSE : TRUE;
}

struct LangMatchContext
{
    WORD wanted = 0;
    bool found = false;
};

// Records whether the wanted language is present, stopping the enumeration
// as soon as it is seen. Used to sidestep FindResourceExW's language
// fallback so Read is an exact-language lookup.
BOOL CALLBACK
OnLanguageMatch( HMODULE, LPCWSTR, LPCWSTR, WORD lang, LONG_PTR param )
{
    auto* ctx = reinterpret_cast< LangMatchContext* >( param );
    if( lang == ctx->wanted ) {
        ctx->found = true;
        return FALSE;  // stop; the match is recorded
    }

    return TRUE;
}

class Win32Engine final : public ResourceEngine
{
public:
    Win32Engine() = default;
    ~Win32Engine() override
    {
        if( m_update != nullptr ) {
            Log::Warn(
                L"Resource update session on '{}' was not committed, "
                L"discarding",
                m_path.wstring() );
            Discard();
        }
    }

    std::error_code Open( const std::filesystem::path& path, OpenMode mode )
        override
    {
        Discard();
        m_module.reset();
        m_path = path;
        m_mode = mode;

        HMODULE module = ::LoadLibraryExW(
            path.c_str(),
            nullptr,
            LOAD_LIBRARY_AS_IMAGE_RESOURCE
                | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE );
        if( module == nullptr ) {
            const auto ec = LastWin32Error();
            Log::Debug(
                L"Failed LoadLibraryExW '{}' [{}]",
                path.wstring(),
                FormatError( ec ) );
            return ec;
        }

        m_module.reset( module );
        return {};
    }

    std::error_code Enumerate( std::vector< ResourceEntry >& entries ) override
    {
        if( !m_module ) {
            return std::make_error_code( std::errc::operation_not_permitted );
        }

        std::vector< ResourceEntry > found;
        EnumContext ctx;
        ctx.module = m_module.get();
        ctx.entries = &found;

        if( !::EnumResourceTypesW(
                m_module.get(),
                OnType,
                reinterpret_cast< LONG_PTR >( &ctx ) ) ) {
            const DWORD code = ::GetLastError();
            if( ctx.ec ) {
                return ctx.ec;
            }

            if( !IsEnumerationEnd( code ) ) {
                const auto ec = Win32Error( code );
                Log::Debug(
                    L"Failed EnumResourceTypesW [{}]", FormatError( ec ) );
                return ec;
            }
        }

        if( ctx.ec ) {
            return ctx.ec;
        }

        entries = std::move( found );
        return {};
    }

    std::error_code Read( const ResourceKey& key, std::vector< uint8_t >& data )
        override
    {
        if( !m_module ) {
            return std::make_error_code( std::errc::operation_not_permitted );
        }

        if( !key.lang ) {
            return make_error_code( errc::invalid_language );
        }

        if( const auto ec = RequireExactLanguage( key ) ) {
            return ec;
        }

        HRSRC found = ::FindResourceExW(
            m_module.get(),
            ToLpcwstr( key.type ),
            ToLpcwstr( key.name ),
            *key.lang );
        if( found == nullptr ) {
            const DWORD code = ::GetLastError();
            return IsNotFound( code )
                ? make_error_code( errc::resource_not_found )
                : Win32Error( code );
        }

        HGLOBAL loaded = ::LoadResource( m_module.get(), found );
        if( loaded == nullptr ) {
            return LastWin32Error();
        }

        const DWORD size = ::SizeofResource( m_module.get(), found );
        const void* bytes = ::LockResource( loaded );
        if( bytes == nullptr && size != 0 ) {
            return LastWin32Error();
        }

        const auto* begin = static_cast< const uint8_t* >( bytes );
        data.assign( begin, begin + size );
        return {};
    }

    std::error_code Write(
        const ResourceKey& key,
        std::span< const uint8_t > data ) override
    {
        if( data.empty() ) {
            return make_error_code( errc::empty_payload );
        }

        if( const auto ec = BeginUpdate( key ) ) {
            return ec;
        }

        // UpdateResourceW takes a non-const pointer but does not modify the
        // data.
        auto* bytes = const_cast< uint8_t* >( data.data() );
        if( !::UpdateResourceW(
                m_update,
                ToLpcwstr( key.type ),
                ToLpcwstr( key.name ),
                *key.lang,
                bytes,
                static_cast< DWORD >( data.size() ) ) ) {
            const auto ec = LastWin32Error();
            Log::Debug( L"Failed UpdateResourceW [{}]", FormatError( ec ) );
            return ec;
        }

        return {};
    }

    std::error_code Remove( const ResourceKey& key ) override
    {
        if( const auto ec = BeginUpdate( key ) ) {
            return ec;
        }

        if( !::UpdateResourceW(
                m_update,
                ToLpcwstr( key.type ),
                ToLpcwstr( key.name ),
                *key.lang,
                nullptr,
                0 ) ) {
            const auto ec = LastWin32Error();
            Log::Debug(
                L"Failed UpdateResourceW (delete) [{}]", FormatError( ec ) );
            return ec;
        }

        return {};
    }

    std::error_code Commit() override
    {
        if( m_update == nullptr ) {
            return {};
        }

        HANDLE handle = m_update;
        m_update = nullptr;
        if( !::EndUpdateResourceW( handle, FALSE ) ) {
            const auto ec = LastWin32Error();
            Log::Debug( L"Failed EndUpdateResourceW [{}]", FormatError( ec ) );
            return ec;
        }

        return {};
    }

    void Discard() override
    {
        if( m_update == nullptr ) {
            return;
        }

        HANDLE handle = m_update;
        m_update = nullptr;
        if( !::EndUpdateResourceW( handle, TRUE ) ) {
            Log::Debug(
                L"Failed EndUpdateResourceW (discard) [{}]",
                FormatError( LastWin32Error() ) );
        }
    }

private:
    // FindResourceExW falls back to another language when the exact one is
    // absent (a single-language resource is returned for any requested
    // language), so an exact-language read cannot rely on it to report a
    // missing language. Confirm the requested language is actually present
    // for this (type, name). Returns {} on an exact match, or
    // errc::resource_not_found when the type, the name, or that specific
    // language is absent.
    std::error_code RequireExactLanguage( const ResourceKey& key )
    {
        LangMatchContext ctx;
        ctx.wanted = *key.lang;

        if( !::EnumResourceLanguagesW(
                m_module.get(),
                ToLpcwstr( key.type ),
                ToLpcwstr( key.name ),
                OnLanguageMatch,
                reinterpret_cast< LONG_PTR >( &ctx ) ) ) {
            // The early stop on a match surfaces as a failure with
            // GetLastError() set to ERROR_RESOURCE_ENUM_USER_STOP; the flag is
            // authoritative.
            if( ctx.found ) {
                return {};
            }

            const DWORD code = ::GetLastError();
            if( IsNotFound( code ) ) {
                return make_error_code( errc::resource_not_found );
            }

            return Win32Error( code );
        }

        return ctx.found ? std::error_code{}
                         : make_error_code( errc::resource_not_found );
    }

    // Validates a write and opens the update session on first use.
    std::error_code BeginUpdate( const ResourceKey& key )
    {
        if( m_mode != OpenMode::ReadWrite ) {
            return make_error_code( errc::read_only );
        }

        if( !key.lang ) {
            return make_error_code( errc::invalid_language );
        }

        if( m_update != nullptr ) {
            return {};
        }

        // The exclusive datafile mapping blocks BeginUpdateResource.
        m_module.reset();

        m_update = ::BeginUpdateResourceW( m_path.c_str(), FALSE );
        if( m_update == nullptr ) {
            const auto ec = LastWin32Error();
            Log::Debug(
                L"Failed BeginUpdateResourceW '{}' [{}]",
                m_path.wstring(),
                FormatError( ec ) );
            return ec;
        }

        return {};
    }

    std::filesystem::path m_path;
    OpenMode m_mode = OpenMode::ReadOnly;
    ModuleHandle m_module;
    HANDLE m_update = nullptr;
};

}  // namespace

std::unique_ptr< ResourceEngine > MakeWin32Engine()
{
    return std::make_unique< Win32Engine >();
}

}  // namespace rcedit
