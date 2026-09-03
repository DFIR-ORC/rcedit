//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include <exception>
#include <span>
#include <string>
#include <vector>

#include <windows.h>

#include "cli/args.h"
#include "cli/commands.h"
#include "core/encoding.h"
#include "core/log.h"
#include "core/output.h"
#include "core/version.h"

namespace {

constexpr int kUsageError = 2;

int Main( std::span< const std::wstring > argv )
{
    using namespace rcedit;
    using namespace rcedit::cli;

    const auto commands = Commands();
    auto parsed = Parse( argv, commands );
    if( !parsed ) {
        Log::Error( L"{}", parsed.error().message );
        const auto* command = parsed.error().command;
        Log::Write(
            Log::Level::Error,
            command ? FormatCommandUsage( *command )
                    : FormatUsage( commands ) );
        return kUsageError;
    }

    if( parsed->version ) {
        Out::Print( L"rcedit {}\n", AnsiToUtf16( Version() ) );
        return 0;
    }

    if( parsed->help ) {
        Out::Write(
            parsed->command ? FormatCommandUsage( *parsed->command )
                            : FormatUsage( commands ) );
        return 0;
    }

    Log::SetLevel(
        parsed->verbose     ? Log::Level::Debug
            : parsed->quiet ? Log::Level::Error
                            : Log::Level::Info );
    return parsed->command->run( *parsed );
}

}  // namespace

int wmain( int argc, wchar_t* argv[] )
{
    ::SetConsoleOutputCP( CP_UTF8 );

    try {
        std::vector< std::wstring > args;
        for( int i = 1; i < argc; ++i ) {
            args.emplace_back( argv[ i ] );
        }

        return Main( args );
    }
    catch( const std::exception& e ) {
        rcedit::Log::Error(
            L"Unexpected exception: {}", rcedit::AnsiToUtf16( e.what() ) );
        return 1;
    }
}
