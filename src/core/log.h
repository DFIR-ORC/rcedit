//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace rcedit::Log {

enum class Level
{
    Debug = 0,
    Info,
    Warn,
    Error
};

void SetLevel( Level level ) noexcept;
[[nodiscard]] Level GetLevel() noexcept;

// Writes "[X] message\n" to stderr as UTF-8. Not for program output.
void Write( Level level, std::wstring_view message );

template < typename... Args >
void Debug( std::wformat_string< Args... > fmt, Args&&... args )
{
    if( GetLevel() <= Level::Debug ) {
        Write(
            Level::Debug, std::format( fmt, std::forward< Args >( args )... ) );
    }
}

template < typename... Args >
void Warn( std::wformat_string< Args... > fmt, Args&&... args )
{
    if( GetLevel() <= Level::Warn ) {
        Write(
            Level::Warn, std::format( fmt, std::forward< Args >( args )... ) );
    }
}

template < typename... Args >
void Error( std::wformat_string< Args... > fmt, Args&&... args )
{
    Write( Level::Error, std::format( fmt, std::forward< Args >( args )... ) );
}

}  // namespace rcedit::Log
