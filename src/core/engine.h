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
#include <memory>
#include <span>
#include <system_error>
#include <vector>

#include "core/resource_id.h"

namespace rcedit {

enum class OpenMode
{
    ReadOnly,
    ReadWrite
};

struct ResourceEntry
{
    ResourceKey key;  // lang is always set
    uint32_t size;
};

// Access to the resources of one PE file. Not thread-safe.
//
// Lifecycle: Open, then any number of Enumerate/Read, then Write/Remove
// (ReadWrite only), then Commit or Discard. Once a Write/Remove was issued,
// Enumerate/Read return std::errc::operation_not_permitted until Commit or
// Discard, after which the engine must be reopened.
class ResourceEngine
{
public:
    virtual ~ResourceEngine() = default;

    [[nodiscard]] virtual std::error_code Open(
        const std::filesystem::path& path,
        OpenMode mode ) = 0;
    [[nodiscard]] virtual std::error_code Enumerate(
        std::vector< ResourceEntry >& entries ) = 0;
    [[nodiscard]] virtual std::error_code Read(
        const ResourceKey& key,
        std::vector< uint8_t >& data ) = 0;

    // key.lang must be set. Empty data is errc::empty_payload.
    [[nodiscard]] virtual std::error_code Write(
        const ResourceKey& key,
        std::span< const uint8_t > data ) = 0;
    [[nodiscard]] virtual std::error_code Remove( const ResourceKey& key ) = 0;

    [[nodiscard]] virtual std::error_code Commit() = 0;
    virtual void Discard() = 0;
};

[[nodiscard]] std::unique_ptr< ResourceEngine > MakeWin32Engine();

}  // namespace rcedit
