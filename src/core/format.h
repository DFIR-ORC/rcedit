//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <format>
#include <string>
#include <system_error>

#include "core/encoding.h"

namespace rcedit {

// "0x2: The system cannot find the file specified." for system errors,
// "rcedit: codec disabled in this build" for rcedit errors.
[[nodiscard]] inline std::wstring FormatError( const std::error_code& ec )
{
    std::wstring message = AnsiToUtf16( ec.message() );
    while( !message.empty()
           && ( message.back() == L'\r' || message.back() == L'\n'
                || message.back() == L' ' ) ) {
        message.pop_back();
    }

    if( ec.category() == std::system_category() ) {
        return std::format(
            L"{:#x}: {}", static_cast< unsigned long >( ec.value() ), message );
    }

    return std::format(
        L"{}: {}", AnsiToUtf16( ec.category().name() ), message );
}

}  // namespace rcedit
