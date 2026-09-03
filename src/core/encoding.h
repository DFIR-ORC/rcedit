//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace rcedit {

[[nodiscard]] std::expected< std::string, std::error_code > Utf16ToUtf8(
    std::wstring_view input );
[[nodiscard]] std::expected< std::wstring, std::error_code > Utf8ToUtf16(
    std::string_view input );

// Lossy conversion of an ANSI code page string (e.g.
// std::error_code::message()).
[[nodiscard]] std::wstring AnsiToUtf16( std::string_view input );

// Case-insensitive comparison of two UTF-16 strings (folded with towlower).
[[nodiscard]] bool EqualsIgnoreCase(
    std::wstring_view a,
    std::wstring_view b ) noexcept;

}  // namespace rcedit
