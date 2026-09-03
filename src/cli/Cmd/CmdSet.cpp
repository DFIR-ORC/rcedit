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

}  // namespace

CommandSpec GetSetCommandSpec()
{
    return {
        L"set", L"Add or replace one resource", kSetOptions, ValidateSet,
        RunSet,
    };
}

}  // namespace rcedit::cli
