//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/commands.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>

#include "core/codec.h"
#include "core/encoding.h"
#include "core/engine.h"
#include "core/error.h"
#include "core/format.h"
#include "core/log.h"
#include "core/ops.h"
#include "core/output.h"
#include "core/resource_id.h"

namespace rcedit::cli {

namespace {

namespace fs = std::filesystem;

constexpr int kOk = 0;
constexpr int kFailure = 1;
constexpr int kUsage = 2;

// ---- shared option specs ----------------------------------------------------

constexpr OptionSpec kTypeRequired = {
    L"type", L't',    true,
    true,    L"TYPE", L"Resource type: RT_RCDATA, RCDATA, #10 or a name"
};
constexpr OptionSpec kTypeOptional = { L"type", L't',
                                       true,    false,
                                       L"TYPE", L"Only this resource type" };
constexpr OptionSpec kNameRequired = {
    L"name", L'n', true, true, L"NAME", L"Resource name: #101 or a string"
};
constexpr OptionSpec kNameOptional = { L"name", L'n',
                                       true,    false,
                                       L"NAME", L"Only this resource name" };
constexpr OptionSpec kLang = {
    L"lang", L'l',    true,
    false,   L"LANG", L"Language id, decimal or 0x hex (default: neutral)"
};
constexpr OptionSpec kRaw = {
    L"raw", 0, false, false, L"", L"Do not decompress a 7z or zstd payload"
};

std::wstring_view CompressHelp()
{
    static const std::wstring help = [] {
        std::wstring text = L"Compress the payload before storing it: ";
        const auto names = AvailableCodecNames();
        for( size_t i = 0; i < names.size(); ++i ) {
            text += names[ i ];
            if( i + 1 < names.size() ) {
                text += L", ";
            }
        }
        return text;
    }();
    return help;
}

constexpr OptionSpec kListOptions[] = { kTypeOptional, kNameOptional };

constexpr OptionSpec kGetOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    { L"output", L'o', true, true, L"FILE", L"Destination file, overwritten" },
    kRaw,
};

const OptionSpec kSetOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    { L"value",
      0,
      true,
      false,
      L"TEXT",
      L"Store TEXT as UTF-8 bytes (no terminator)" },
    { L"value-utf16",
      0,
      true,
      false,
      L"TEXT",
      L"Store TEXT as UTF-16LE bytes (no terminator)" },
    { L"value-path", 0, true, false, L"FILE", L"Store the content of FILE" },
    { L"compress", L'c', true, false, L"CODEC", CompressHelp() },
    { L"output",
      L'o',
      true,
      false,
      L"FILE",
      L"Write to a copy of <pe_file> at FILE instead of in place" },
};

constexpr OptionSpec kRemoveOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    { L"output",
      L'o',
      true,
      false,
      L"FILE",
      L"Write to a copy of <pe_file> at FILE instead of in place" },
};

constexpr OptionSpec kHexdumpOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    kRaw,
    { L"limit", 0, true, false, L"N", L"Show at most N bytes" },
};

// ---- value parsing shared by validate and run
// --------------------------------

std::expected< ResourceKey, std::wstring > ParseKey(
    const ParsedArgs& args,
    bool typeRequired )
{
    ResourceKey key;

    if( const auto type = args.Value( L"type" ) ) {
        auto parsed = ParseResourceType( *type );
        if( !parsed ) {
            return std::unexpected(
                std::format( L"invalid --type '{}'", *type ) );
        }
        key.type = std::move( *parsed );
    }
    else if( typeRequired ) {
        return std::unexpected( L"missing required option '--type'" );
    }

    if( const auto name = args.Value( L"name" ) ) {
        auto parsed = ParseResourceName( *name );
        if( !parsed ) {
            return std::unexpected(
                std::format( L"invalid --name '{}'", *name ) );
        }
        key.name = std::move( *parsed );
    }

    if( const auto lang = args.Value( L"lang" ) ) {
        auto parsed = ParseLang( *lang );
        if( !parsed ) {
            return std::unexpected(
                std::format( L"invalid --lang '{}'", *lang ) );
        }
        key.lang = *parsed;
    }

    return key;
}

std::expected< CodecId, std::wstring > ParseCompress( const ParsedArgs& args )
{
    const auto name = args.Value( L"compress" );
    if( !name ) {
        return CodecId::None;
    }
    const auto id = ParseCodecName( *name );
    if( !id ) {
        return std::unexpected(
            std::format( L"unknown --compress codec '{}'", *name ) );
    }
    if( *id != CodecId::None && !IsCodecAvailable( *id ) ) {
        return std::unexpected(
            std::format(
                L"--compress codec '{}' is disabled in this build", *name ) );
    }
    return *id;
}

std::expected< std::optional< size_t >, std::wstring > ParseLimit(
    const ParsedArgs& args )
{
    const auto text = args.Value( L"limit" );
    if( !text ) {
        return std::nullopt;
    }
    if( text->empty() || !std::ranges::all_of( *text, []( wchar_t c ) {
            return c >= L'0' && c <= L'9';
        } ) ) {
        return std::unexpected( std::format( L"invalid --limit '{}'", *text ) );
    }
    size_t value = 0;
    for( wchar_t c : *text ) {
        const size_t digit = static_cast< size_t >( c - L'0' );
        if( value > ( std::numeric_limits< size_t >::max() - digit ) / 10 ) {
            return std::unexpected(
                std::format( L"--limit '{}' is too large", *text ) );
        }
        value = value * 10 + digit;
    }
    return value;
}

std::optional< fs::path > OutputPath( const ParsedArgs& args )
{
    const auto value = args.Value( L"output" );
    if( !value ) {
        return std::nullopt;
    }
    return fs::path( std::wstring( *value ) );
}

std::expected< std::vector< uint8_t >, std::error_code > ReadFile(
    const fs::path& path )
{
    std::ifstream in( path, std::ios::binary );
    if( !in ) {
        return std::unexpected(
            std::make_error_code( std::errc::no_such_file_or_directory ) );
    }
    std::vector< uint8_t > data(
        ( std::istreambuf_iterator< char >( in ) ),
        std::istreambuf_iterator< char >() );
    if( in.bad() ) {
        return std::unexpected( std::make_error_code( std::errc::io_error ) );
    }
    return data;
}

std::error_code WriteFile(
    const fs::path& path,
    std::span< const uint8_t > data )
{
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    if( !out ) {
        return std::make_error_code( std::errc::permission_denied );
    }
    out.write(
        reinterpret_cast< const char* >( data.data() ),
        static_cast< std::streamsize >( data.size() ) );
    if( !out ) {
        return std::make_error_code( std::errc::io_error );
    }
    return {};
}

// Explains an operation failure; lists candidate languages when ambiguous.
int Report(
    const std::error_code& ec,
    const fs::path& pe,
    const ResourceKey& key )
{
    if( ec == errc::ambiguous_language ) {
        auto engine = MakeWin32Engine();
        ResourceKey resolved;
        std::vector< uint16_t > candidates;
        if( !engine->Open( pe, OpenMode::ReadOnly )
            && ResolveLanguage( *engine, key, resolved, candidates ) == ec ) {
            std::wstring list;
            for( size_t i = 0; i < candidates.size(); ++i ) {
                list +=
                    std::format( L"{}{}", i ? L", " : L"", candidates[ i ] );
            }
            Log::Error(
                L"Resource {}/{} exists in several languages ({}), specify "
                L"--lang",
                FormatResourceType( key.type ),
                FormatResourceName( key.name ),
                list );
            return kFailure;
        }
    }

    Log::Error( L"Failed on '{}' [{}]", pe.wstring(), FormatError( ec ) );
    return kFailure;
}

// ---- validate
// -----------------------------------------------------------------

std::optional< std::wstring > ValidateList( const ParsedArgs& args )
{
    if( auto key = ParseKey( args, false ); !key ) {
        return key.error();
    }
    return std::nullopt;
}

std::optional< std::wstring > ValidateKeyOnly( const ParsedArgs& args )
{
    if( auto key = ParseKey( args, true ); !key ) {
        return key.error();
    }
    return std::nullopt;
}

std::optional< std::wstring > ValidateSet( const ParsedArgs& args )
{
    if( auto key = ParseKey( args, true ); !key ) {
        return key.error();
    }
    const int sources = ( args.Has( L"value" ) ? 1 : 0 )
        + ( args.Has( L"value-utf16" ) ? 1 : 0 )
        + ( args.Has( L"value-path" ) ? 1 : 0 );
    if( sources != 1 ) {
        return L"exactly one of --value, --value-utf16 or --value-path is "
               L"required";
    }
    if( auto codec = ParseCompress( args ); !codec ) {
        return codec.error();
    }
    return std::nullopt;
}

std::optional< std::wstring > ValidateHexdump( const ParsedArgs& args )
{
    if( auto key = ParseKey( args, true ); !key ) {
        return key.error();
    }
    if( auto limit = ParseLimit( args ); !limit ) {
        return limit.error();
    }
    return std::nullopt;
}

// ---- run
// ------------------------------------------------------------------------

int RunList( const ParsedArgs& args )
{
    const auto key = ParseKey( args, false );
    if( !key ) {
        return kUsage;
    }
    ListOptions options;
    if( args.Has( L"type" ) ) {
        options.type = key->type;
    }
    if( args.Has( L"name" ) ) {
        options.name = key->name;
    }

    auto engine = MakeWin32Engine();
    std::vector< ListEntry > entries;
    if( const auto ec = List( *engine, args.pePath, options, entries ) ) {
        return Report( ec, args.pePath, *key );
    }

    size_t typeWidth = 4;
    size_t nameWidth = 4;
    for( const auto& e : entries ) {
        typeWidth =
            std::max( typeWidth, FormatResourceType( e.key.type ).size() );
        nameWidth =
            std::max( nameWidth, FormatResourceName( e.key.name ).size() );
    }

    Out::Print(
        L"{:<{}}  {:<{}}  {:>6}  {:>10}  {:<5}  {:>10}\n",
        L"TYPE",
        typeWidth,
        L"NAME",
        nameWidth,
        L"LANG",
        L"SIZE",
        L"CODEC",
        L"CONTENT" );
    for( const auto& e : entries ) {
        std::wstring codec = L"-";
        std::wstring content = L"-";
        if( e.codec != CodecId::None ) {
            codec = std::wstring( CodecName( e.codec ) );
            if( e.contentSize ) {
                content = std::format( L"{}", *e.contentSize );
            }
            else if( !IsCodecAvailable( e.codec ) ) {
                content = L"?";
            }
        }
        Out::Print(
            L"{:<{}}  {:<{}}  {:>6}  {:>10}  {:<5}  {:>10}\n",
            FormatResourceType( e.key.type ),
            typeWidth,
            FormatResourceName( e.key.name ),
            nameWidth,
            *e.key.lang,
            e.size,
            codec,
            content );
    }
    return kOk;
}

int RunGet( const ParsedArgs& args )
{
    const auto key = ParseKey( args, true );
    if( !key ) {
        return kUsage;
    }
    auto engine = MakeWin32Engine();
    std::vector< uint8_t > data;
    if( const auto ec =
            Get( *engine, args.pePath, *key, args.Has( L"raw" ), data ) ) {
        return Report( ec, args.pePath, *key );
    }

    const auto outputPath = OutputPath( args );
    if( !outputPath ) {
        return kUsage;
    }
    const auto output = *outputPath;
    if( const auto ec = WriteFile( output, data ) ) {
        Log::Error(
            L"Failed to write '{}' [{}]", output.wstring(), FormatError( ec ) );
        return kFailure;
    }
    Log::Info( L"Wrote {} bytes to '{}'", data.size(), output.wstring() );
    return kOk;
}

int RunSet( const ParsedArgs& args )
{
    const auto key = ParseKey( args, true );
    if( !key ) {
        return kUsage;
    }
    const auto codec = ParseCompress( args );
    if( !codec ) {
        return kUsage;
    }

    std::vector< uint8_t > data;
    if( const auto value = args.Value( L"value" ) ) {
        const auto utf8 = Utf16ToUtf8( *value );
        if( !utf8 ) {
            Log::Error(
                L"--value is not valid UTF-16 [{}]",
                FormatError( utf8.error() ) );
            return kFailure;
        }
        data.assign( utf8->begin(), utf8->end() );
    }
    else if( const auto value16 = args.Value( L"value-utf16" ) ) {
        const auto* bytes =
            reinterpret_cast< const uint8_t* >( value16->data() );
        data.assign( bytes, bytes + value16->size() * sizeof( wchar_t ) );
    }
    else {
        const auto valuePath = args.Value( L"value-path" );
        if( !valuePath ) {
            return kUsage;
        }
        const fs::path path{ std::wstring( *valuePath ) };
        auto content = ReadFile( path );
        if( !content ) {
            Log::Error(
                L"Failed to read '{}' [{}]",
                path.wstring(),
                FormatError( content.error() ) );
            return kFailure;
        }
        data = std::move( *content );
    }

    auto engine = MakeWin32Engine();
    if( const auto ec = Set(
            *engine, args.pePath, *key, data, *codec, OutputPath( args ) ) ) {
        return Report( ec, args.pePath, *key );
    }
    Log::Info(
        L"Set {}/{} ({} bytes{})",
        FormatResourceType( key->type ),
        FormatResourceName( key->name ),
        data.size(),
        *codec == CodecId::None
            ? L""
            : std::format( L", {} compressed", CodecName( *codec ) ) );
    return kOk;
}

int RunRemove( const ParsedArgs& args )
{
    const auto key = ParseKey( args, true );
    if( !key ) {
        return kUsage;
    }
    auto engine = MakeWin32Engine();
    if( const auto ec =
            Remove( *engine, args.pePath, *key, OutputPath( args ) ) ) {
        return Report( ec, args.pePath, *key );
    }
    Log::Info(
        L"Removed {}/{}",
        FormatResourceType( key->type ),
        FormatResourceName( key->name ) );
    return kOk;
}

int RunHexdump( const ParsedArgs& args )
{
    const auto key = ParseKey( args, true );
    if( !key ) {
        return kUsage;
    }
    const auto limit = ParseLimit( args );
    if( !limit ) {
        return kUsage;
    }
    auto engine = MakeWin32Engine();
    std::wstring text;
    if( const auto ec = Hexdump(
            *engine, args.pePath, *key, args.Has( L"raw" ), *limit, text ) ) {
        return Report( ec, args.pePath, *key );
    }
    Out::Write( text );
    return kOk;
}

const CommandSpec kCommands[] = {
    { L"list",
      L"List resources with size and detected compression",
      kListOptions,
      ValidateList,
      RunList },
    { L"get",
      L"Extract one resource to a file (decompressed unless --raw)",
      kGetOptions,
      ValidateKeyOnly,
      RunGet },
    { L"set",
      L"Add or replace one resource",
      kSetOptions,
      ValidateSet,
      RunSet },
    { L"remove",
      L"Delete one resource",
      kRemoveOptions,
      ValidateKeyOnly,
      RunRemove },
    { L"hexdump",
      L"Print one resource as hex and ASCII",
      kHexdumpOptions,
      ValidateHexdump,
      RunHexdump },
};

}  // namespace

std::span< const CommandSpec > Commands()
{
    return kCommands;
}

}  // namespace rcedit::cli
