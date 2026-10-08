//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace rcedit {

// Numeric id (MAKEINTRESOURCE) or string name.
using ResourceId = std::variant< uint16_t, std::wstring >;

struct ResourceKey
{
    ResourceId type;
    ResourceId name;
    std::optional< uint16_t > lang;  // nullopt: not specified, resolved by ops

    bool operator==( const ResourceKey& ) const = default;
};

// "#n" -> numeric (1..65535), anything else non-empty -> string.
[[nodiscard]] std::expected< ResourceId, std::error_code > ParseResourceName(
    std::wstring_view text );

// Like ParseResourceName, plus RT_* aliases (with or without "RT_",
// case-insensitive).
[[nodiscard]] std::expected< ResourceId, std::error_code > ParseResourceType(
    std::wstring_view text );

// Decimal or "0x" hex, 0..65535.
[[nodiscard]] std::expected< uint16_t, std::error_code > ParseLang(
    std::wstring_view text );

[[nodiscard]] std::wstring FormatResourceName(
    const ResourceId& id );  // "#n" or string
[[nodiscard]] std::wstring FormatResourceType(
    const ResourceId& id );  // "RT_X", "#n" or string

// Win32 boundary. The returned pointer aliases 'id' for string names.
[[nodiscard]] const wchar_t* ToLpcwstr( const ResourceId& id ) noexcept;
[[nodiscard]] ResourceId FromLpcwstr( const wchar_t* value );

}  // namespace rcedit
