//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/Cmd/CmdSet.h"

#include <format>

#include "cli/Cmd/CmdCommon.h"
#include "core/codec.h"
#include "core/encoding.h"
#include "core/engine.h"
#include "core/format.h"
#include "core/log.h"
#include "core/ops.h"
#include "core/resource_id.h"

namespace rcedit::cli {

namespace {

namespace fs = std::filesystem;

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

}  // namespace

std::error_code HandleSet( const ParsedArgs& args )
{
    const auto key = ParseKey( args, true );
    if( !key ) {
        Log::Error( L"{}", key.error() );
        return std::make_error_code( std::errc::invalid_argument );
    }

    const auto codec = ParseCompress( args );
    if( !codec ) {
        Log::Error( L"{}", codec.error() );
        return std::make_error_code( std::errc::invalid_argument );
    }

    // Prepare data
    std::vector< uint8_t > data;
    if( const auto value = args.Value( L"value" ) ) {
        const auto utf8 = Utf16ToUtf8( *value );
        if( !utf8 ) {
            Log::Error(
                L"--value is not valid UTF-16 [{}]",
                FormatError( utf8.error() ) );
            return utf8.error();
        }

        data.assign( utf8->begin(), utf8->end() );
    }
    else if( const auto value16 = args.Value( L"value-utf16" ) ) {
        const auto* bytes =
            reinterpret_cast< const uint8_t* >( value16->data() );
        data.assign( bytes, bytes + value16->size() * sizeof( wchar_t ) );
    }
    else if( const auto valuePath = args.Value( L"value-path" ) ) {
        const fs::path path{ std::wstring( *valuePath ) };
        auto content = ReadFile( path );
        if( !content ) {
            Log::Error(
                L"Failed to read '{}' [{}]",
                path.wstring(),
                FormatError( content.error() ) );
            return content.error();
        }

        data = std::move( *content );
    }
    else {
        Log::Error( L"No input provided for 'set'" );
        return std::make_error_code( std::errc::invalid_argument );
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
    return {};
}

CommandSpec GetSetCommandSpec()
{
    return {
        L"set",    L"Add or replace one resource", kSetOptions, ValidateSet,
        HandleSet,
    };
}

}  // namespace rcedit::cli
