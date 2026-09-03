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
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace rcedit {

enum class CodecId
{
    None,
    SevenZip,
    Zstd
};

class Codec
{
public:
    virtual ~Codec() = default;

    [[nodiscard]] virtual CodecId Id() const noexcept = 0;
    [[nodiscard]] virtual std::wstring_view Name() const noexcept = 0;

    // Empty input is errc::empty_payload.
    [[nodiscard]] virtual std::error_code Compress(
        std::span< const uint8_t > input,
        std::vector< uint8_t >& output ) = 0;
    [[nodiscard]] virtual std::error_code Decompress(
        std::span< const uint8_t > input,
        std::vector< uint8_t >& output ) = 0;

    // Decompressed size recorded in the container, if any.
    [[nodiscard]] virtual std::optional< uint64_t > ContentSize(
        std::span< const uint8_t > input ) = 0;
};

// Magic-byte detection. Always compiled, regardless of enabled codecs.
[[nodiscard]] CodecId DetectCodec( std::span< const uint8_t > data ) noexcept;

[[nodiscard]] std::wstring_view CodecName( CodecId id ) noexcept;
[[nodiscard]] std::expected< CodecId, std::error_code > ParseCodecName(
    std::wstring_view name );

// errc::codec_disabled for a codec compiled out; std::errc::invalid_argument
// for None.
[[nodiscard]] std::expected< Codec*, std::error_code > FindCodec( CodecId id );
[[nodiscard]] bool IsCodecAvailable( CodecId id ) noexcept;

// "none" plus every codec compiled in, for help text.
[[nodiscard]] std::vector< std::wstring_view > AvailableCodecNames();

#ifdef RCEDIT_HAS_ZSTD
[[nodiscard]] Codec& ZstdCodec();
#endif
#ifdef RCEDIT_HAS_7Z
[[nodiscard]] Codec& SevenZipCodec();
#endif

}  // namespace rcedit
