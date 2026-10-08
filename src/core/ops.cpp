//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/ops.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include <windows.h>

#include "core/error.h"
#include "core/format.h"
#include "core/log.h"

namespace rcedit {

namespace {

namespace fs = std::filesystem;

std::error_code ReadResolved(
    ResourceEngine& engine,
    const fs::path& pe,
    const ResourceKey& key,
    ResourceKey& resolved,
    std::vector< uint8_t >& stored )
{
    if( const auto ec = engine.Open( pe, OpenMode::ReadOnly ) ) {
        return ec;
    }

    std::vector< uint16_t > candidates;
    if( const auto ec = ResolveLanguage( engine, key, resolved, candidates ) ) {
        return ec;
    }

    return engine.Read( resolved, stored );
}

// Shared by the hexdump's ascii pane and the list preview so the two always
// agree on what counts as printable.
constexpr wchar_t PrintableOrDot( uint8_t byte )
{
    return ( byte >= 0x20 && byte <= 0x7E ) ? static_cast< wchar_t >( byte )
                                            : L'.';
}

std::error_code MaybeDecompress( std::vector< uint8_t >& data, bool raw )
{
    const CodecId detected = DetectCodec( data );
    if( raw || detected == CodecId::None ) {
        return {};
    }

    const auto codec = FindCodec( detected );
    if( !codec ) {
        Log::Debug(
            L"Payload is {} compressed but that codec is not built in",
            CodecName( detected ) );
        return codec.error();
    }

    std::vector< uint8_t > unpacked;
    if( const auto ec = ( *codec )->Decompress( data, unpacked ) ) {
        return ec;
    }

    data = std::move( unpacked );
    return {};
}

// Selects the path to modify ('output' if given, else 'pe') and rejects a
// self-update target. Does not copy: callers that also have a
// possibly-failing step before the first file mutation (e.g. Set's compress)
// should run it between this and CopyToOutput so a doomed operation never
// leaves a stray copy at 'output'.
std::error_code SelectTarget(
    const fs::path& pe,
    const std::optional< fs::path >& output,
    fs::path& target )
{
    target = output ? *output : pe;

    if( IsRunningExecutable( target ) ) {
        return make_error_code( errc::self_update );
    }

    return {};
}

// Copies 'pe' to 'output' when requested. Never opens 'pe' for writing.
std::error_code CopyToOutput(
    const fs::path& pe,
    const std::optional< fs::path >& output )
{
    if( !output ) {
        return {};
    }

    std::error_code ec;
    fs::copy_file( pe, *output, fs::copy_options::overwrite_existing, ec );
    if( ec ) {
        Log::Debug(
            L"Failed to copy '{}' to '{}' [{}]",
            pe.wstring(),
            output->wstring(),
            FormatError( ec ) );
    }

    return ec;
}

// Another process holding the PE open without sharing writes -- anti-virus
// scanning a freshly linked binary, the search indexer, an earlier step of the
// same build -- fails the update until it lets go.
bool IsContention( const std::error_code& ec )
{
    if( ec.category() != std::system_category() ) {
        return false;
    }

    switch( ec.value() ) {
        // EndUpdateResourceW reports the write conflict as ERROR_OPEN_FAILED.
        case ERROR_OPEN_FAILED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_ACCESS_DENIED:
        case ERROR_USER_MAPPED_FILE:
            return true;
        default:
            return false;
    }
}

// Runs the whole open/modify/commit sequence again on contention rather than
// resuming it: Open()'s exclusive mapping, BeginUpdateResourceW and
// EndUpdateResourceW can each be the one to hit the conflict, and the last of
// them consumes the update session whether or not it succeeds, so there is
// nothing left to resume from. 'update' must therefore be self-contained and
// leave no session behind on failure.
template < typename Update >
std::error_code RetryOnContention( Update&& update )
{
    constexpr int kMaxAttempts = 10;
    constexpr std::chrono::milliseconds kRetryDelay( 250 );

    for( int attempt = 1;; ++attempt ) {
        const auto ec = update();
        if( !ec || attempt == kMaxAttempts || !IsContention( ec ) ) {
            return ec;
        }

        Log::Warn(
            L"Resource update held up by another process, retrying "
            L"({}/{}) [{}]",
            attempt,
            kMaxAttempts,
            FormatError( ec ) );

        std::this_thread::sleep_for( kRetryDelay );
    }
}

// Best-effort cleanup for a failure that occurs after CopyToOutput has
// already created 'output': removes it so a doomed Set/Remove does not leave
// a stray copy behind. A no-op when 'output' was not given. The remove's own
// error is ignored; 'pe' is never touched.
void RemoveStrayOutput( const std::optional< fs::path >& output )
{
    if( !output ) {
        return;
    }

    std::error_code ec;
    fs::remove( *output, ec );
}

}  // namespace

bool IsRunningExecutable( const fs::path& path )
{
    wchar_t self[ MAX_PATH ];
    const DWORD length = ::GetModuleFileNameW( nullptr, self, MAX_PATH );
    if( length == 0 || length >= MAX_PATH ) {
        return false;
    }

    std::error_code ec;
    const bool same = fs::equivalent( path, fs::path( self ), ec );
    return !ec && same;
}

std::error_code ResolveLanguage(
    ResourceEngine& engine,
    const ResourceKey& key,
    ResourceKey& resolved,
    std::vector< uint16_t >& candidates )
{
    candidates.clear();
    if( key.lang ) {
        resolved = key;
        return {};
    }

    std::vector< ResourceEntry > entries;
    if( const auto ec = engine.Enumerate( entries ) ) {
        return ec;
    }

    for( const auto& entry : entries ) {
        if( entry.key.type == key.type && entry.key.name == key.name ) {
            candidates.push_back( *entry.key.lang );
        }
    }

    if( candidates.empty() ) {
        return make_error_code( errc::resource_not_found );
    }

    resolved = key;
    if( std::ranges::find( candidates, uint16_t( 0 ) ) != candidates.end() ) {
        resolved.lang = 0;
        candidates.clear();
        return {};
    }

    if( candidates.size() == 1 ) {
        resolved.lang = candidates.front();
        candidates.clear();
        return {};
    }

    std::ranges::sort( candidates );
    return make_error_code( errc::ambiguous_language );
}

std::error_code List(
    ResourceEngine& engine,
    const fs::path& pe,
    const ListOptions& options,
    std::vector< ListEntry >& out )
{
    if( const auto ec = engine.Open( pe, OpenMode::ReadOnly ) ) {
        return ec;
    }

    std::vector< ResourceEntry > entries;
    if( const auto ec = engine.Enumerate( entries ) ) {
        return ec;
    }

    std::vector< ListEntry > result;
    for( const auto& entry : entries ) {
        if( options.type && entry.key.type != *options.type ) {
            continue;
        }

        if( options.name && entry.key.name != *options.name ) {
            continue;
        }

        std::vector< uint8_t > stored;
        if( const auto ec = engine.Read( entry.key, stored ) ) {
            return ec;
        }

        // Taken from the bytes as stored: List never decompresses, so a packed
        // resource previews its codec header, not its payload.
        std::vector< uint8_t > preview(
            stored.begin(),
            stored.begin()
                + static_cast< std::ptrdiff_t >(
                    std::min( kPreviewBytes, stored.size() ) ) );

        ListEntry item{ entry.key,
                        entry.size,
                        DetectCodec( stored ),
                        std::nullopt,
                        std::move( preview ) };
        if( item.codec != CodecId::None ) {
            if( const auto codec = FindCodec( item.codec ) ) {
                item.contentSize = ( *codec )->ContentSize( stored );
            }
        }

        result.push_back( std::move( item ) );
    }

    out = std::move( result );
    return {};
}

std::error_code Get(
    ResourceEngine& engine,
    const fs::path& pe,
    const ResourceKey& key,
    bool raw,
    std::vector< uint8_t >& out,
    GetResult* result )
{
    ResourceKey resolved;
    std::vector< uint8_t > stored;
    if( const auto ec = ReadResolved( engine, pe, key, resolved, stored ) ) {
        return ec;
    }

    // Both are read before MaybeDecompress swaps in the unpacked payload: the
    // stored size is otherwise lost, and under 'raw' the detected codec is the
    // only sign that what the caller gets back is still compressed.
    const auto storedSize = static_cast< uint64_t >( stored.size() );
    const auto detected = DetectCodec( stored );

    if( const auto ec = MaybeDecompress( stored, raw ) ) {
        return ec;
    }

    if( result ) {
        *result = GetResult{ *resolved.lang, storedSize, detected };
    }

    out = std::move( stored );
    return {};
}

std::error_code Set(
    ResourceEngine& engine,
    const fs::path& pe,
    const ResourceKey& key,
    std::span< const uint8_t > data,
    CodecId codecId,
    const std::optional< fs::path >& output,
    SetResult* result )
{
    if( data.empty() ) {
        return make_error_code( errc::empty_payload );
    }

    fs::path target;
    if( const auto ec = SelectTarget( pe, output, target ) ) {
        return ec;
    }

    std::vector< uint8_t > packed;
    std::span< const uint8_t > payload = data;
    if( codecId != CodecId::None ) {
        const auto codec = FindCodec( codecId );
        if( !codec ) {
            return codec.error();
        }

        if( const auto ec = ( *codec )->Compress( data, packed ) ) {
            return ec;
        }

        payload = packed;
    }

    if( const auto ec = CopyToOutput( pe, output ) ) {
        return ec;
    }

    ResourceKey resolved = key;
    if( !resolved.lang ) {
        resolved.lang = 0;
    }

    const auto ec = RetryOnContention( [ & ]() -> std::error_code {
        if( const auto ec = engine.Open( target, OpenMode::ReadWrite ) ) {
            return ec;
        }

        if( const auto ec = engine.Write( resolved, payload ) ) {
            engine.Discard();
            return ec;
        }

        return engine.Commit();
    } );

    if( ec ) {
        RemoveStrayOutput( output );
        return ec;
    }

    if( result ) {
        *result = SetResult{ *resolved.lang,
                             static_cast< uint64_t >( data.size() ),
                             static_cast< uint64_t >( payload.size() ),
                             codecId };
    }

    return ec;
}

std::error_code Remove(
    ResourceEngine& engine,
    const fs::path& pe,
    const ResourceKey& key,
    const std::optional< fs::path >& output,
    RemoveResult* result )
{
    fs::path target;
    if( const auto ec = SelectTarget( pe, output, target ) ) {
        return ec;
    }

    if( const auto ec = CopyToOutput( pe, output ) ) {
        return ec;
    }

    // Set by whichever attempt resolves the language; only read once the whole
    // sequence has succeeded, so a retried attempt overwriting it is harmless.
    uint16_t removedLang = 0;

    const auto ec = RetryOnContention( [ & ]() -> std::error_code {
        if( const auto ec = engine.Open( target, OpenMode::ReadWrite ) ) {
            return ec;
        }

        // Resolved inside the retry: a new attempt re-opens the file, so the
        // language has to be resolved again against that session. Caller-side
        // reporting of the candidates on errc::ambiguous_language re-resolves
        // them on its own read-only engine (see cli/Cmd/CmdCommon.cpp).
        ResourceKey resolved;
        std::vector< uint16_t > candidates;
        if( const auto ec =
                ResolveLanguage( engine, key, resolved, candidates ) ) {
            engine.Discard();
            return ec;
        }

        removedLang = *resolved.lang;

        if( const auto ec = engine.Remove( resolved ) ) {
            engine.Discard();
            return ec;
        }

        return engine.Commit();
    } );

    if( ec ) {
        RemoveStrayOutput( output );
        return ec;
    }

    if( result ) {
        *result = RemoveResult{ removedLang };
    }

    return ec;
}

std::error_code Hexdump(
    ResourceEngine& engine,
    const fs::path& pe,
    const ResourceKey& key,
    bool raw,
    std::optional< size_t > limit,
    std::wstring& out )
{
    std::vector< uint8_t > data;
    if( const auto ec = Get( engine, pe, key, raw, data ) ) {
        return ec;
    }

    out = FormatHexdump( data, limit );
    return {};
}

std::wstring FormatPreview( std::span< const uint8_t > data )
{
    if( data.empty() ) {
        return L"-";
    }

    std::wstring out;
    out.reserve( data.size() );
    for( const uint8_t byte : data ) {
        out += PrintableOrDot( byte );
    }

    return out;
}

std::wstring FormatHexdump(
    std::span< const uint8_t > data,
    std::optional< size_t > limit )
{
    if( data.empty() ) {
        return L"<empty>\n";
    }

    const size_t shown = limit ? std::min( *limit, data.size() ) : data.size();
    constexpr size_t kPerLine = 16;
    constexpr size_t kHexWidth =
        kPerLine * 3 + 1;  // "XX " * 16 plus the mid gap

    std::wstring out;
    for( size_t offset = 0; offset < shown; offset += kPerLine ) {
        const size_t count = std::min( kPerLine, shown - offset );

        std::wstring hex;
        for( size_t i = 0; i < count; ++i ) {
            hex += std::format( L"{:02X} ", data[ offset + i ] );
            if( i == 7 ) {
                hex += L' ';
            }
        }

        hex.resize( kHexWidth, L' ' );

        std::wstring ascii;
        for( size_t i = 0; i < count; ++i ) {
            ascii += PrintableOrDot( data[ offset + i ] );
        }

        out += std::format( L"{:08X}  {} |{}|\n", offset, hex, ascii );
    }

    if( shown < data.size() ) {
        out += std::format( L"... ({} more bytes)\n", data.size() - shown );
    }

    return out;
}

}  // namespace rcedit
