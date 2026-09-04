//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/Cmd/CmdList.h"

#include <algorithm>
#include <format>

#include "cli/Cmd/CmdCommon.h"
#include "core/codec.h"
#include "core/engine.h"
#include "core/log.h"
#include "core/ops.h"
#include "core/output.h"
#include "core/resource_id.h"

namespace rcedit::cli {

namespace {

constexpr OptionSpec kListOptions[] = { kTypeOptional, kNameOptional };

std::optional< std::wstring > ValidateList( const ParsedArgs& args )
{
    if( auto key = ParseKey( args, false ); !key ) {
        return key.error();
    }

    return std::nullopt;
}

}  // namespace

std::error_code HandleList( const ParsedArgs& args )
{
    const auto key = ParseKey( args, false );
    if( !key ) {
        Log::Error( L"{}", key.error() );
        return std::make_error_code( std::errc::invalid_argument );
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
        L"{:<{}}  {:<{}}  {:>6}  {:>10}  {:<5}  {:>10}  {}\n",
        L"TYPE",
        typeWidth,
        L"NAME",
        nameWidth,
        L"LANG",
        L"SIZE",
        L"CODEC",
        L"UNPACKED",
        L"PREVIEW" );
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

        // The preview is last and unpadded: nothing follows it to align to.
        Out::Print(
            L"{:<{}}  {:<{}}  {:>6}  {:>10}  {:<5}  {:>10}  {}\n",
            FormatResourceType( e.key.type ),
            typeWidth,
            FormatResourceName( e.key.name ),
            nameWidth,
            *e.key.lang,
            e.size,
            codec,
            content,
            FormatPreview( e.preview ) );
    }

    return {};
}

CommandSpec GetListCommandSpec()
{
    return {
        L"list",      L"List resources with size and detected compression",
        kListOptions, ValidateList,
        HandleList,
    };
}

}  // namespace rcedit::cli
