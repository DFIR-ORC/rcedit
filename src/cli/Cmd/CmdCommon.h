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
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "cli/args.h"
#include "core/codec.h"
#include "core/resource_id.h"

namespace rcedit::cli {

// Exit codes shared by every command.
inline constexpr int kOk = 0;
inline constexpr int kFailure = 1;
inline constexpr int kUsage = 2;

// ---- shared option specs ----------------------------------------------------

inline constexpr OptionSpec kTypeRequired = {
    L"type", L't',    true,
    true,    L"TYPE", L"Resource type: RT_RCDATA, RCDATA, #10 or a name"
};
inline constexpr OptionSpec kTypeOptional = {
    L"type", L't', true, false, L"TYPE", L"Only this resource type"
};
inline constexpr OptionSpec kNameRequired = {
    L"name", L'n', true, true, L"NAME", L"Resource name: #101 or a string"
};
inline constexpr OptionSpec kNameOptional = {
    L"name", L'n', true, false, L"NAME", L"Only this resource name"
};
inline constexpr OptionSpec kLang = {
    L"lang", L'l',    true,
    false,   L"LANG", L"Language id, decimal or 0x hex (default: neutral)"
};
inline constexpr OptionSpec kRaw = {
    L"raw", 0, false, false, L"", L"Do not decompress a 7z or zstd payload"
};

// ---- value parsing shared by validate and run -------------------------------

[[nodiscard]] std::expected< ResourceKey, std::wstring > ParseKey(
    const ParsedArgs& args,
    bool typeRequired );

[[nodiscard]] std::expected< CodecId, std::wstring > ParseCompress(
    const ParsedArgs& args );

[[nodiscard]] std::expected< std::optional< size_t >, std::wstring > ParseLimit(
    const ParsedArgs& args );

[[nodiscard]] std::optional< std::filesystem::path > OutputPath(
    const ParsedArgs& args );

[[nodiscard]] std::expected< std::vector< uint8_t >, std::error_code > ReadFile(
    const std::filesystem::path& path );

[[nodiscard]] std::error_code WriteFile(
    const std::filesystem::path& path,
    std::span< const uint8_t > data );

// Explains an operation failure; lists candidate languages when ambiguous.
int Report(
    const std::error_code& ec,
    const std::filesystem::path& pe,
    const ResourceKey& key );

// Validate for commands whose only inputs are --type/--name/--lang.
[[nodiscard]] std::optional< std::wstring > ValidateKeyOnly(
    const ParsedArgs& args );

}  // namespace rcedit::cli
