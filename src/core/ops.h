//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "core/codec.h"
#include "core/engine.h"
#include "core/resource_id.h"

namespace rcedit {

struct ListOptions
{
    std::optional< ResourceId > type;
    std::optional< ResourceId > name;
};

// Leading bytes of a resource kept for a text preview. Sized to one screen
// column, not to a parse: nothing reads a header out of this.
inline constexpr size_t kPreviewBytes = 16;

struct ListEntry
{
    ResourceKey key;  // lang always set
    uint32_t size;
    CodecId codec;  // detected by magic, None if plain
    std::optional< uint64_t > contentSize;  // decompressed size when the codec
                                            // is available and records it
    std::vector< uint8_t > preview;  // first kPreviewBytes as stored, so a
                                     // compressed resource shows its codec
                                     // header rather than its payload
};

[[nodiscard]] std::error_code List(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ListOptions& options,
    std::vector< ListEntry >& out );

// Decompresses by magic unless 'raw'. A detected but disabled codec is
// errc::codec_disabled.
[[nodiscard]] std::error_code Get(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    bool raw,
    std::vector< uint8_t >& out );

// key.lang unset means neutral. With 'output', 'pe' is copied there first and
// only the copy is modified.
[[nodiscard]] std::error_code Set(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    std::span< const uint8_t > data,
    CodecId codec,
    const std::optional< std::filesystem::path >& output );

[[nodiscard]] std::error_code Remove(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    const std::optional< std::filesystem::path >& output );

[[nodiscard]] std::error_code Hexdump(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    bool raw,
    std::optional< size_t > limit,
    std::wstring& out );

// Language resolution on an engine that is already open and readable.
// Fills 'candidates' on errc::ambiguous_language.
[[nodiscard]] std::error_code ResolveLanguage(
    ResourceEngine& engine,
    const ResourceKey& key,
    ResourceKey& resolved,
    std::vector< uint16_t >& candidates );

[[nodiscard]] std::wstring FormatHexdump(
    std::span< const uint8_t > data,
    std::optional< size_t > limit );

// One line of text for a ListEntry::preview: printable bytes as themselves,
// anything else as '.', and "-" for a resource with no bytes at all.
[[nodiscard]] std::wstring FormatPreview( std::span< const uint8_t > data );

[[nodiscard]] bool IsRunningExecutable( const std::filesystem::path& path );

}  // namespace rcedit
