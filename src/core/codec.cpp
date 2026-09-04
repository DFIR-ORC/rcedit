//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/codec.h"

#include <algorithm>
#include <array>

#include "core/encoding.h"
#include "core/error.h"

namespace rcedit {

namespace {

constexpr std::array< uint8_t, 6 > k7zMagic = { 0x37, 0x7A, 0xBC,
                                                0xAF, 0x27, 0x1C };
constexpr std::array< uint8_t, 4 > kZstdMagic = { 0x28, 0xB5, 0x2F, 0xFD };

template < size_t N >
bool StartsWith(
    std::span< const uint8_t > data,
    const std::array< uint8_t, N >& magic )
{
    return data.size() >= N
        && std::equal( magic.begin(), magic.end(), data.begin() );
}

}  // namespace

CodecId DetectCodec( std::span< const uint8_t > data ) noexcept
{
    if( StartsWith( data, k7zMagic ) ) {
        return CodecId::SevenZip;
    }

    if( StartsWith( data, kZstdMagic ) ) {
        return CodecId::Zstd;
    }

    return CodecId::None;
}

std::wstring_view CodecName( CodecId id ) noexcept
{
    switch( id ) {
        case CodecId::SevenZip:
            return L"7z";
        case CodecId::Zstd:
            return L"zstd";
        case CodecId::None:
            break;
    }

    return L"none";
}

std::expected< CodecId, std::error_code > ParseCodecName(
    std::wstring_view name )
{
    for( const CodecId id :
         { CodecId::None, CodecId::SevenZip, CodecId::Zstd } ) {
        if( EqualsIgnoreCase( name, CodecName( id ) ) ) {
            return id;
        }
    }

    return std::unexpected( make_error_code( errc::unknown_codec ) );
}

bool IsCodecAvailable( CodecId id ) noexcept
{
    switch( id ) {
        case CodecId::SevenZip:
#ifdef RCEDIT_HAS_7Z
            return true;
#else
            return false;
#endif
        case CodecId::Zstd:
#ifdef RCEDIT_HAS_ZSTD
            return true;
#else
            return false;
#endif
        case CodecId::None:
            break;
    }

    return false;
}

std::expected< Codec*, std::error_code > FindCodec( CodecId id )
{
    switch( id ) {
        case CodecId::SevenZip:
#ifdef RCEDIT_HAS_7Z
            return &SevenZipCodec();
#else
            return std::unexpected( make_error_code( errc::codec_disabled ) );
#endif
        case CodecId::Zstd:
#ifdef RCEDIT_HAS_ZSTD
            return &ZstdCodec();
#else
            return std::unexpected( make_error_code( errc::codec_disabled ) );
#endif
        case CodecId::None:
            break;
    }

    return std::unexpected(
        std::make_error_code( std::errc::invalid_argument ) );
}

std::vector< std::wstring_view > AvailableCodecNames()
{
    std::vector< std::wstring_view > names = { CodecName( CodecId::None ) };
    for( const CodecId id : { CodecId::SevenZip, CodecId::Zstd } ) {
        if( IsCodecAvailable( id ) ) {
            names.push_back( CodecName( id ) );
        }
    }

    return names;
}

}  // namespace rcedit
