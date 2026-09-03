//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/Cmd/CmdGet.h"

#include "cli/Cmd/CmdCommon.h"
#include "core/engine.h"
#include "core/format.h"
#include "core/log.h"
#include "core/ops.h"

namespace rcedit::cli {

namespace {

constexpr OptionSpec kGetOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    { L"output", L'o', true, true, L"FILE", L"Destination file, overwritten" },
    kRaw,
};

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

}  // namespace

CommandSpec GetGetCommandSpec()
{
    return {
        L"get",
        L"Extract one resource to a file (decompressed unless --raw)",
        kGetOptions,
        ValidateKeyOnly,
        RunGet,
    };
}

}  // namespace rcedit::cli
