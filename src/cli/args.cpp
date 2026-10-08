//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/args.h"

#include <algorithm>
#include <format>

#include "core/encoding.h"
#include "core/version.h"

namespace rcedit::cli {

namespace {

struct GlobalFlag
{
    std::wstring_view longName;
    wchar_t shortName;
    std::wstring_view help;
    bool ParsedArgs::* field;
};

constexpr GlobalFlag kGlobals[] = {
    { L"help",
      L'h',
      L"Show this help, or a command's help",
      &ParsedArgs::help },
    { L"version", 0, L"Show version", &ParsedArgs::version },
    { L"verbose", L'v', L"Debug logging on stderr", &ParsedArgs::verbose },
    { L"quiet", L'q', L"Errors only on stderr", &ParsedArgs::quiet },
};

bool MatchesFlag(
    std::wstring_view token,
    std::wstring_view longName,
    wchar_t shortName )
{
    if( token.size() > 2 && token.starts_with( L"--" ) ) {
        return token.substr( 2 ) == longName;
    }

    return shortName != 0 && token.size() == 2 && token[ 0 ] == L'-'
        && token[ 1 ] == shortName;
}

const OptionSpec* FindOption(
    const CommandSpec& command,
    std::wstring_view token )
{
    for( const auto& option : command.options ) {
        if( MatchesFlag( token, option.longName, option.shortName ) ) {
            return &option;
        }
    }

    return nullptr;
}

const CommandSpec* FindCommand(
    std::span< const CommandSpec > commands,
    std::wstring_view name )
{
    for( const auto& command : commands ) {
        if( command.name == name ) {
            return &command;
        }
    }

    return nullptr;
}

std::unexpected< UsageError > Fail(
    std::wstring message,
    const CommandSpec* command )
{
    return std::unexpected( UsageError{ std::move( message ), command } );
}

std::wstring FlagLabel( wchar_t shortName, std::wstring_view longName )
{
    return shortName != 0 ? std::format( L"-{}, --{}", shortName, longName )
                          : std::format( L"    --{}", longName );
}

std::wstring OptionLabel( const OptionSpec& option )
{
    std::wstring label = FlagLabel( option.shortName, option.longName );
    if( option.takesValue ) {
        label += std::format( L" <{}>", option.valueName );
    }

    return label;
}

}  // namespace

std::expected< ParsedArgs, UsageError > Parse(
    std::span< const std::wstring > argv,
    std::span< const CommandSpec > commands )
{
    ParsedArgs args;
    bool endOfOptions = false;
    bool havePath = false;

    for( size_t i = 0; i < argv.size(); ++i ) {
        const std::wstring_view token = argv[ i ];

        if( !endOfOptions && token == L"--" ) {
            endOfOptions = true;
            continue;
        }

        const bool looksLikeOption =
            !endOfOptions && token.size() > 1 && token.front() == L'-';
        if( looksLikeOption ) {
            bool matchedGlobal = false;
            for( const auto& global : kGlobals ) {
                if( MatchesFlag( token, global.longName, global.shortName ) ) {
                    args.*( global.field ) = true;
                    matchedGlobal = true;
                    break;
                }
            }

            if( matchedGlobal ) {
                continue;
            }

            if( args.command == nullptr ) {
                return Fail(
                    std::format( L"unknown option '{}'", token ), nullptr );
            }

            const OptionSpec* option = FindOption( *args.command, token );
            if( option == nullptr ) {
                return Fail(
                    std::format(
                        L"unknown option '{}' for command '{}'",
                        token,
                        args.command->name ),
                    args.command );
            }

            if( args.Has( option->longName ) ) {
                return Fail(
                    std::format(
                        L"option '--{}' given more than once",
                        option->longName ),
                    args.command );
            }

            std::wstring value;
            if( option->takesValue ) {
                if( i + 1 >= argv.size() ) {
                    return Fail(
                        std::format(
                            L"option '--{}' requires a value",
                            option->longName ),
                        args.command );
                }

                value = argv[ ++i ];
            }

            args.values.emplace(
                std::wstring( option->longName ), std::move( value ) );
            continue;
        }

        // Positional.
        if( args.command == nullptr ) {
            args.command = FindCommand( commands, token );
            if( args.command == nullptr ) {
                return Fail(
                    std::format( L"unknown command '{}'", token ), nullptr );
            }

            continue;
        }

        if( !havePath ) {
            args.pePath = std::wstring( token );
            havePath = true;
            continue;
        }

        return Fail(
            std::format( L"unexpected argument '{}'", token ), args.command );
    }

    if( args.help || args.version ) {
        return args;
    }

    if( args.command == nullptr ) {
        return Fail( L"missing command", nullptr );
    }

    if( !havePath ) {
        return Fail( L"missing <pe_file>", args.command );
    }

    if( args.verbose && args.quiet ) {
        return Fail(
            L"--verbose and --quiet are mutually exclusive", args.command );
    }

    for( const auto& option : args.command->options ) {
        if( option.required && !args.Has( option.longName ) ) {
            return Fail(
                std::format(
                    L"missing required option '--{}'", option.longName ),
                args.command );
        }
    }

    if( args.command->validate != nullptr ) {
        if( auto message = args.command->validate( args ) ) {
            return Fail( std::move( *message ), args.command );
        }
    }

    return args;
}

std::wstring FormatUsage( std::span< const CommandSpec > commands )
{
    std::wstring out = std::format(
        L"rcedit {} - Edit resources of Windows PE files\n\n",
        AnsiToUtf16( Version() ) );
    out += L"Usage: rcedit [global options] <command> <pe_file> [options]\n\n";

    out += L"Commands:\n";
    size_t width = 0;
    for( const auto& command : commands ) {
        width = std::max( width, command.name.size() );
    }

    for( const auto& command : commands ) {
        out += std::format(
            L"  {:<{}}  {}\n", command.name, width, command.summary );
    }

    out += L"\nGlobal options:\n";
    size_t globalWidth = 0;
    for( const auto& global : kGlobals ) {
        globalWidth = std::max(
            globalWidth,
            FlagLabel( global.shortName, global.longName ).size() );
    }

    for( const auto& global : kGlobals ) {
        out += std::format(
            L"  {:<{}}  {}\n",
            FlagLabel( global.shortName, global.longName ),
            globalWidth,
            global.help );
    }

    out += L"\nRun 'rcedit <command> --help' for the options of a command.\n";
    return out;
}

std::wstring FormatCommandUsage( const CommandSpec& command )
{
    std::wstring out = std::format(
        L"Usage: rcedit {} <pe_file> [options]\n\n{}\n",
        command.name,
        command.summary );

    if( command.options.empty() ) {
        return out;
    }

    size_t width = 0;
    for( const auto& option : command.options ) {
        width = std::max( width, OptionLabel( option ).size() );
    }

    out += L"\nOptions:\n";
    for( const auto& option : command.options ) {
        out += std::format(
            L"  {:<{}}  {}{}\n",
            OptionLabel( option ),
            width,
            option.help,
            option.required ? L" (required)" : L"" );
    }

    return out;
}

}  // namespace rcedit::cli
