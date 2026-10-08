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
    std::vector< ResourceEntry >* entries = nullptr;
    std::error_code ec;
    ResourceId type;
    ResourceId name;
};

// These callbacks are invoked by kernel32 across a plain C frame; no
// exception (e.g. std::bad_alloc from a std::wstring/push_back/new) may
// escape. Returning FALSE stops enumeration, which Enumerate() reports as
// ctx.ec/GetLastError, so a caught exception is surfaced as an error rather
// than a silently-truncated success.
BOOL CALLBACK OnLanguage(
    HMODULE module,
    LPCWSTR type,
    LPCWSTR name,
    WORD lang,
    LONG_PTR param )
{
    auto* ctx = reinterpret_cast< EnumContext* >( param );

    try {
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
    catch( ... ) {
        ctx->ec = std::make_error_code( std::errc::not_enough_memory );
        return FALSE;
    }
}

BOOL CALLBACK
OnName( HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param )
{
    auto* ctx = reinterpret_cast< EnumContext* >( param );

    try {
        ctx->name = FromLpcwstr( name );

        if( !::EnumResourceLanguagesW(
                module, type, name, OnLanguage, param ) ) {
            const DWORD code = ::GetLastError();
            if( !IsEnumerationEnd( code ) ) {
                ctx->ec = Win32Error( code );
                return FALSE;
            }
        }

        return ctx->ec ? FALSE : TRUE;
    }
    catch( ... ) {
        ctx->ec = std::make_error_code( std::errc::not_enough_memory );
        return FALSE;
    }
}

BOOL CALLBACK OnType( HMODULE module, LPWSTR type, LONG_PTR param )
{
    auto* ctx = reinterpret_cast< EnumContext* >( param );

    try {
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
    catch( ... ) {
        ctx->ec = std::make_error_code( std::errc::not_enough_memory );
        return FALSE;
    }
}

// Where the optional header starts, relative to the NT headers.
constexpr LONGLONG kOptionalHeaderOffset =
    offsetof( IMAGE_NT_HEADERS32, OptionalHeader );
static_assert(
    kOptionalHeaderOffset == offsetof( IMAGE_NT_HEADERS64, OptionalHeader ),
    "PE32 and PE32+ must agree on where the optional header starts" );

// CheckSum sits at the same place in both optional headers: ImageBase's four
// extra bytes make up for the BaseOfData that PE32+ does not have.
constexpr WORD kCheckSumInOptional =
    offsetof( IMAGE_OPTIONAL_HEADER32, CheckSum );
static_assert(
    kCheckSumInOptional == offsetof( IMAGE_OPTIONAL_HEADER64, CheckSum ),
    "PE32 and PE32+ must agree on where the checksum sits" );

// The data directories, on the other hand, start further in for PE32+, whose
// four 64-bit fields push them back by sixteen bytes.
constexpr WORD kSecurityInOptional32 =
    offsetof( IMAGE_OPTIONAL_HEADER32, DataDirectory )
    + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof( IMAGE_DATA_DIRECTORY );
constexpr WORD kSecurityInOptional64 =
    offsetof( IMAGE_OPTIONAL_HEADER64, DataDirectory )
    + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof( IMAGE_DATA_DIRECTORY );
constexpr WORD kRvaCountInOptional32 =
    offsetof( IMAGE_OPTIONAL_HEADER32, NumberOfRvaAndSizes );
constexpr WORD kRvaCountInOptional64 =
    offsetof( IMAGE_OPTIONAL_HEADER64, NumberOfRvaAndSizes );

// Smallest optional header that still holds a checksum.
constexpr WORD kMinOptionalHeaderSize = kCheckSumInOptional + sizeof( DWORD );

// File offsets of the two header fields a resource update invalidates,
// resolved once from the DOS and NT headers.
struct HeaderFields
{
    LONGLONG checkSum = 0;  // OptionalHeader.CheckSum
    LONGLONG security = 0;  // DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY],
                            // 0 when the optional header does not reach it
};

std::error_code ReadAt( HANDLE file, LONGLONG offset, void* buffer, DWORD size )
{
    LARGE_INTEGER position;
    position.QuadPart = offset;
    if( !::SetFilePointerEx( file, position, nullptr, FILE_BEGIN ) ) {
        return LastWin32Error();
    }

    DWORD read = 0;
    if( !::ReadFile( file, buffer, size, &read, nullptr ) ) {
        return LastWin32Error();
    }

    // Short of the requested bytes means the headers run past the end of the
    // file, which is not a PE this code can reason about.
    return read == size ? std::error_code{}
                        : Win32Error( ERROR_BAD_EXE_FORMAT );
}

std::error_code WriteAt(
    HANDLE file,
    LONGLONG offset,
    const void* buffer,
    DWORD size )
{
    LARGE_INTEGER position;
    position.QuadPart = offset;
    if( !::SetFilePointerEx( file, position, nullptr, FILE_BEGIN ) ) {
        return LastWin32Error();
    }

    DWORD written = 0;
    if( !::WriteFile( file, buffer, size, &written, nullptr ) ) {
        return LastWin32Error();
    }

    return written == size ? std::error_code{}
                           : Win32Error( ERROR_WRITE_FAULT );
}

// Walks the DOS and file headers to locate the fields a commit has to fix
// up. Anything that does not parse as a PE is ERROR_BAD_EXE_FORMAT.
std::error_code FindHeaderFields( HANDLE file, HeaderFields& fields )
{
    IMAGE_DOS_HEADER dos = {};
    if( const auto ec = ReadAt( file, 0, &dos, sizeof( dos ) ) ) {
        return ec;
    }

    if( dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ) {
        return Win32Error( ERROR_BAD_EXE_FORMAT );
    }

    DWORD signature = 0;
    if( const auto ec =
            ReadAt( file, dos.e_lfanew, &signature, sizeof( signature ) ) ) {
        return ec;
    }

    if( signature != IMAGE_NT_SIGNATURE ) {
        return Win32Error( ERROR_BAD_EXE_FORMAT );
    }

    IMAGE_FILE_HEADER header = {};
    if( const auto ec = ReadAt(
            file,
            dos.e_lfanew + sizeof( signature ),
            &header,
            sizeof( header ) ) ) {
        return ec;
    }

    // An object file or a stripped image can carry an optional header too
    // short to reach the checksum; there is then nothing to clear.
    if( header.SizeOfOptionalHeader < kMinOptionalHeaderSize ) {
        return Win32Error( ERROR_BAD_EXE_FORMAT );
    }

    const LONGLONG optional = dos.e_lfanew + kOptionalHeaderOffset;

    WORD magic = 0;
    if( const auto ec = ReadAt( file, optional, &magic, sizeof( magic ) ) ) {
        return ec;
    }

    if( magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        && magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ) {
        return Win32Error( ERROR_BAD_EXE_FORMAT );
    }

    HeaderFields found;
    found.checkSum = optional + kCheckSumInOptional;

    // The certificate table is the fifth data directory; an image is free to
    // declare fewer, or an optional header too short to hold them.
    const bool pe64 = magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    const WORD securityInOptional =
        pe64 ? kSecurityInOptional64 : kSecurityInOptional32;
    if( header.SizeOfOptionalHeader
        < securityInOptional + sizeof( IMAGE_DATA_DIRECTORY ) ) {
        fields = found;
        return {};
    }

    DWORD directories = 0;
    if( const auto ec = ReadAt(
            file,
            optional + ( pe64 ? kRvaCountInOptional64 : kRvaCountInOptional32 ),
            &directories,
            sizeof( directories ) ) ) {
        return ec;
    }

    if( directories > IMAGE_DIRECTORY_ENTRY_SECURITY ) {
        found.security = optional + securityInOptional;
    }

    fields = found;
    return {};
}

// Zeroes OptionalHeader.CheckSum when the image carries one, reporting in
// 'cleared' the value that was there. EndUpdateResourceW rewrites the
// resource directory and shifts what follows it without touching the
// checksum, so the value left in the header no longer matches the bytes on
// disk; zero is what a linker writes when it computes none, and what the
// loader accepts for anything that is not a driver or a boot-time DLL.
//
// TODO: recompute the checksum rather than clear it, for the images that do
// need a valid one. The algorithm is a 16-bit ones' complement sum over the
// whole file plus its size, with the checksum field itself read as zero;
// imagehlp's CheckSumMappedFile does it but would add a DLL outside the
// import allowlist, so it has to be written here.
std::error_code ClearCheckSum( HANDLE file, LONGLONG offset, DWORD& cleared )
{
    cleared = 0;

    DWORD checksum = 0;
    if( const auto ec =
            ReadAt( file, offset, &checksum, sizeof( checksum ) ) ) {
        return ec;
    }

    if( checksum == 0 ) {
        return {};
    }

    constexpr DWORD kNoCheckSum = 0;
    if( const auto ec =
            WriteAt( file, offset, &kNoCheckSum, sizeof( kNoCheckSum ) ) ) {
        return ec;
    }

    cleared = checksum;
    return {};
}

// Zeroes the certificate table's data directory entry when the image
// declares one, reporting in 'cleared' the entry that was there.
//
// EndUpdateResourceW drops the certificate bytes -- the file comes back
// shorter by exactly the size the entry claims -- but leaves the entry
// itself in place, pointing past the end of the file. What is left is not a
// signature that merely fails to verify but a dangling offset, so the entry
// goes rather than being kept. Nothing has to be truncated: the bytes it
// pointed at are already gone.
std::error_code ClearSecurityDirectory(
    HANDLE file,
    LONGLONG offset,
    IMAGE_DATA_DIRECTORY& cleared )
{
    cleared = {};

    IMAGE_DATA_DIRECTORY entry = {};
    if( const auto ec = ReadAt( file, offset, &entry, sizeof( entry ) ) ) {
        return ec;
    }

    if( entry.VirtualAddress == 0 && entry.Size == 0 ) {
        return {};
    }

    constexpr IMAGE_DATA_DIRECTORY kNoSecurity = {};
    if( const auto ec =
            WriteAt( file, offset, &kNoSecurity, sizeof( kNoSecurity ) ) ) {
        return ec;
    }

    cleared = entry;
    return {};
}

// Drops what the resource update just invalidated in the PE headers: the
// certificate table entry, then the checksum.
std::error_code ClearStaleHeaders( const std::filesystem::path& path )
{
    FileHandle file(
        ::CreateFileW(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr ) );
    if( !file ) {
        return LastWin32Error();
    }

    HeaderFields fields;
    if( const auto ec = FindHeaderFields( file.get(), fields ) ) {
        return ec;
    }

    if( fields.security != 0 ) {
        IMAGE_DATA_DIRECTORY security = {};
        if( const auto ec = ClearSecurityDirectory(
                file.get(), fields.security, security ) ) {
            return ec;
        }

        if( security.Size != 0 || security.VirtualAddress != 0 ) {
            Log::Warn(
                L"Removed the certificate table of '{}' ({} bytes at offset "
                L"0x{:X}), invalidated by the resource update: the file is no "
                L"longer signed",
                path.wstring(),
                security.Size,
                security.VirtualAddress );
        }
    }

    DWORD checksum = 0;
    if( const auto ec =
            ClearCheckSum( file.get(), fields.checkSum, checksum ) ) {
        return ec;
    }

    if( checksum != 0 ) {
        Log::Debug(
            L"Cleared the stale PE checksum 0x{:08X} of '{}', a new one is not "
            L"computed",
            checksum,
            path.wstring() );
    }

    return {};
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

        // Best effort: the resources are already written, and stale headers
        // are exactly what the caller would have been left with before this
        // step existed, so a failure warns instead of failing the commit.
        // Reporting it would also feed the contention retry in ops.cpp,
        // which replays the whole sequence -- for Remove, against a resource
        // that is already gone.
        if( const auto ec = ClearStaleHeaders( m_path ) ) {
            Log::Warn(
                L"Failed to clear the stale PE headers of '{}'; its checksum "
                L"and certificate table, if any, no longer match the file "
                L"[{}]",
                m_path.wstring(),
                FormatError( ec ) );
        }

        return {};
    }

    void Discard() override
    {
        // Also releases the exclusive read-only mapping from Open(), even
        // when no update session was started yet (e.g. Remove() failed to
        // resolve its target before ever calling Write/Remove). Without
        // this, a caller that deletes the underlying file right after a
        // discarded session (e.g. to clean up a doomed --output copy) would
        // hit a sharing violation, since Open()'s LOAD_LIBRARY_AS_DATAFILE_
        // EXCLUSIVE mapping does not grant FILE_SHARE_DELETE.
        m_module.reset();

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
            // ERROR_RESOURCE_DATA_NOT_FOUND (1812) is also a not-found here:
            // IsNotFound() only covers type/name/lang (1813-1815).
            if( IsNotFound( code ) || code == ERROR_RESOURCE_DATA_NOT_FOUND ) {
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
