//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/log.h"

#include <cstdio>
#include <print>

#include "core/encoding.h"

namespace rcedit::Log {

namespace {

Level g_level = Level::Info;

constexpr char Tag( Level level )
{
    switch( level ) {
        case Level::Debug:
            return 'D';
        case Level::Info:
            return 'I';
        case Level::Warn:
            return 'W';
        case Level::Error:
            return 'E';
    }

    return '?';
}

}  // namespace

void SetLevel( Level level ) noexcept
{
    g_level = level;
}

Level GetLevel() noexcept
{
    return g_level;
}

void Write( Level level, std::wstring_view message )
{
    const auto utf8 = Utf16ToUtf8( message );
    std::print(
        stderr,
        "[{}] {}\n",
        Tag( level ),
        utf8 ? *utf8 : std::string( "<unencodable message>" ) );
    std::fflush( stderr );
}

}  // namespace rcedit::Log
