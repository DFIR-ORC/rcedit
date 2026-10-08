//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "core/codec.h"

namespace rcedit {

// Single-entry 7z archive, LZMA2 level 9, entry named "payload".
class SevenZipCodecImpl final : public Codec
{
public:
    [[nodiscard]] CodecId Id() const noexcept override
    {
        return CodecId::SevenZip;
    }
    [[nodiscard]] std::wstring_view Name() const noexcept override
    {
        return L"7z";
    }
    [[nodiscard]] std::error_code Compress(
        std::span< const uint8_t > input,
        std::vector< uint8_t >& output ) override;
    [[nodiscard]] std::error_code Decompress(
        std::span< const uint8_t > input,
        std::vector< uint8_t >& output ) override;
    [[nodiscard]] std::optional< uint64_t > ContentSize(
        std::span< const uint8_t > input ) override;
};

}  // namespace rcedit
