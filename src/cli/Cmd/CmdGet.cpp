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

}  // namespace

std::error_code HandleGet( const ParsedArgs& args )
{
    const auto key = ParseKey( args, true );
    if( !key ) {
        Log::Error( L"{}", key.error() );
        return std::make_error_code( std::errc::invalid_argument );
    }

    const auto outputPath = OutputPath( args );
    if( !outputPath ) {
        Log::Error( L"--output is required for 'get'" );
        return std::make_error_code( std::errc::invalid_argument );
    }

    auto engine = MakeWin32Engine();
    const bool raw = args.Has( L"raw" );
    std::vector< uint8_t > data;
    GetResult result;
    if( const auto ec =
            Get( *engine, args.pePath, *key, raw, data, &result ) ) {
        return Report( ec, args.pePath, *key );
    }

    if( const auto ec = WriteFile( *outputPath, data ) ) {
        Log::Error(
            L"Failed to write '{}' [{}]",
            outputPath->wstring(),
            FormatError( ec ) );
        return ec;
    }

    const ConfirmationField fields[] = {
        { L"Type", FormatTypeVerbose( key->type ) },
        { L"Name", FormatResourceName( key->name ) },
        { L"Lang", FormatLang( result.lang ) },
        { L"Stored",
          result.codec == CodecId::None
              ? std::format( L"{} bytes", result.storedSize )
              : std::format(
                    L"{} bytes ({})",
                    result.storedSize,
                    CodecName( result.codec ) ) },
        // Under --raw the compressed payload is what reaches the file, so the
        // Stored row above is not the size of what was written.
        { L"Wrote",
          std::format(
              L"{} bytes to '{}'{}",
              data.size(),
              outputPath->wstring(),
              raw ? L" (raw)" : L"" ) },
    };

    PrintConfirmation(
        std::format( L"Extracted resource from '{}'", args.pePath.wstring() ),
        fields );
    return {};
}

CommandSpec GetGetCommandSpec()
{
    return {
        L"get",
        L"Extract one resource to a file (decompressed unless --raw)",
        kGetOptions,
        ValidateKeyOnly,
        HandleGet,
    };
}

}  // namespace rcedit::cli
