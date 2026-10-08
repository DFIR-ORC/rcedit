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

constexpr int kFailure = 1;
constexpr int kUsageError = 2;

int Main( std::span< const std::wstring > argv )
{
    using namespace rcedit;
    using namespace rcedit::cli;

    const auto commands = Commands();
    auto parsed = Parse( argv, commands );
    if( !parsed ) {
        Log::Error( L"{}", parsed.error().message );

        // The message is a diagnostic and stays tagged; the usage block that
        // follows it is a page of help text, so it goes out undecorated -- on
        // stderr, since stdout belongs to the command that failed to run.
        const auto* command = parsed.error().command;
        Out::WriteErr(
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
    if( const auto ec = parsed->command->handle( *parsed ) ) {
        return kFailure;
    }

    return 0;
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
