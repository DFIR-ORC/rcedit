//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/Cmd/CmdRemove.h"

#include "cli/Cmd/CmdCommon.h"
#include "core/engine.h"
#include "core/log.h"
#include "core/ops.h"
#include "core/resource_id.h"

namespace rcedit::cli {

namespace {

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

}  // namespace

CommandSpec GetRemoveCommandSpec()
{
    return {
        L"remove", L"Delete one resource", kRemoveOptions, ValidateKeyOnly,
        RunRemove,
    };
}

}  // namespace rcedit::cli
