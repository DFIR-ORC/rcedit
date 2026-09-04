//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace rcedit::cli {

struct OptionSpec
{
    std::wstring_view longName;  // without dashes
    wchar_t shortName;  // 0 if none
    bool takesValue;
    bool required;
    std::wstring_view valueName;  // shown in help when takesValue
    std::wstring_view help;
};

struct ParsedArgs;

struct CommandSpec
{
    std::wstring_view name;
    std::wstring_view summary;
    std::span< const OptionSpec > options;
    std::optional< std::wstring > ( *validate )(
        const ParsedArgs& args );  // message on failure, may be null
    std::error_code ( *handle )( const ParsedArgs& args );  // empty on success
};

struct ParsedArgs
{
    const CommandSpec* command = nullptr;
    std::filesystem::path pePath;
    std::map< std::wstring, std::wstring, std::less<> >
        values;  // longName -> value ("" for flags)
    bool help = false;
    bool version = false;
    bool verbose = false;
    bool quiet = false;

    [[nodiscard]] bool Has( std::wstring_view longName ) const
    {
        return values.contains( longName );
    }

    [[nodiscard]] std::optional< std::wstring_view > Value(
        std::wstring_view longName ) const
    {
        const auto it = values.find( longName );
        if( it == values.end() ) {
            return std::nullopt;
        }

        return std::wstring_view( it->second );
    }
};

struct UsageError
{
    std::wstring message;
    const CommandSpec* command;  // known command, or null
};

// 'argv' excludes the program name. Pure: no I/O, no Win32.
[[nodiscard]] std::expected< ParsedArgs, UsageError > Parse(
    std::span< const std::wstring > argv,
    std::span< const CommandSpec > commands );

[[nodiscard]] std::wstring FormatUsage(
    std::span< const CommandSpec > commands );
[[nodiscard]] std::wstring FormatCommandUsage( const CommandSpec& command );

}  // namespace rcedit::cli
