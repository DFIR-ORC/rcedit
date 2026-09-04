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

// What Set actually wrote, filled only on success.
struct SetResult
{
    uint16_t lang = 0;  // language written, 0 when none was requested
    uint64_t inputSize = 0;  // payload as handed to Set
    uint64_t storedSize = 0;  // bytes placed in the PE, packed if a
                              // codec applied
    CodecId codec = CodecId::None;  // codec applied, None if plain
};

// What Get actually read, filled only on success.
struct GetResult
{
    uint16_t lang = 0;  // language the key resolved to
    uint64_t storedSize = 0;  // bytes held in the PE, before any
                              // decompression
    CodecId codec = CodecId::None;  // codec detected, None if plain
};

// What Remove actually deleted, filled only on success.
struct RemoveResult
{
    uint16_t lang = 0;  // language the key resolved to
};

// Decompresses by magic unless 'raw'. A detected but disabled codec is
// errc::codec_disabled.
[[nodiscard]] std::error_code Get(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    bool raw,
    std::vector< uint8_t >& out,
    GetResult* result = nullptr );

// key.lang unset means neutral. With 'output', 'pe' is copied there first and
// only the copy is modified.
[[nodiscard]] std::error_code Set(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    std::span< const uint8_t > data,
    CodecId codec,
    const std::optional< std::filesystem::path >& output,
    SetResult* result = nullptr );

[[nodiscard]] std::error_code Remove(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    const std::optional< std::filesystem::path >& output,
    RemoveResult* result = nullptr );

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
