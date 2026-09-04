//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/Cmd/CmdHexdump.h"

#include "cli/Cmd/CmdCommon.h"
#include "core/engine.h"
#include "core/log.h"
#include "core/ops.h"
#include "core/output.h"

namespace rcedit::cli {

namespace {

constexpr OptionSpec kHexdumpOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    kRaw,
    { L"limit", 0, true, false, L"N", L"Show at most N bytes" },
};

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

}  // namespace

std::error_code HandleHexdump( const ParsedArgs& args )
{
    const auto key = ParseKey( args, true );
    if( !key ) {
        Log::Error( L"{}", key.error() );
        return std::make_error_code( std::errc::invalid_argument );
    }

    const auto limit = ParseLimit( args );
    if( !limit ) {
        Log::Error( L"{}", limit.error() );
        return std::make_error_code( std::errc::invalid_argument );
    }

    auto engine = MakeWin32Engine();
    std::wstring text;
    if( const auto ec = Hexdump(
            *engine, args.pePath, *key, args.Has( L"raw" ), *limit, text ) ) {
        return Report( ec, args.pePath, *key );
    }

    Out::Write( text );
    return {};
}

CommandSpec GetHexdumpCommandSpec()
{
    return {
        L"hexdump",      L"Print one resource as hex and ASCII",
        kHexdumpOptions, ValidateHexdump,
        HandleHexdump,
    };
}

}  // namespace rcedit::cli
