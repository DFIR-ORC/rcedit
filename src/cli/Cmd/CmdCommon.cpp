//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/Cmd/CmdCommon.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>

#include "core/engine.h"
#include "core/error.h"
#include "core/format.h"
#include "core/log.h"
#include "core/ops.h"

namespace rcedit::cli {

namespace fs = std::filesystem;

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

std::optional< std::wstring > ValidateKeyOnly( const ParsedArgs& args )
{
    if( auto key = ParseKey( args, true ); !key ) {
        return key.error();
    }

    return std::nullopt;
}

}  // namespace rcedit::cli
