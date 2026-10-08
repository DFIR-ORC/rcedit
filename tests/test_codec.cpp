//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include <algorithm>
#include <memory>

#include "core/codec.h"
#include "core/error.h"

#ifdef RCEDIT_HAS_ZSTD
#    include <zstd.h>
#endif

namespace rcedit::test {

namespace {

std::vector< uint8_t > Pattern( size_t size )
{
    std::vector< uint8_t > v( size );
    for( size_t i = 0; i < size; ++i ) {
        v[ i ] = static_cast< uint8_t >( ( i * 7 ) % 251 );
    }

    return v;
}

void DetectsMagics()
{
    const uint8_t sevenZip[] = {
        0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, 0x00, 0x04
    };
    const uint8_t zstd[] = { 0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x00 };
    const uint8_t plain[] = { 'h', 'e', 'l', 'l', 'o' };
    const uint8_t shortSevenZip[] = { 0x37, 0x7A, 0xBC };

    CHECK( DetectCodec( sevenZip ) == CodecId::SevenZip );
    CHECK( DetectCodec( zstd ) == CodecId::Zstd );
    CHECK( DetectCodec( plain ) == CodecId::None );
    CHECK( DetectCodec( shortSevenZip ) == CodecId::None );
    CHECK( DetectCodec( std::span< const uint8_t >() ) == CodecId::None );
}

void NamesRoundTrip()
{
    CHECK( CodecName( CodecId::None ) == L"none" );
    CHECK( CodecName( CodecId::SevenZip ) == L"7z" );
    CHECK( CodecName( CodecId::Zstd ) == L"zstd" );

    auto a = ParseCodecName( L"7z" );
    CHECK( a.has_value() && *a == CodecId::SevenZip );
    auto b = ParseCodecName( L"ZSTD" );
    CHECK( b.has_value() && *b == CodecId::Zstd );
    auto c = ParseCodecName( L"none" );
    CHECK( c.has_value() && *c == CodecId::None );
    auto d = ParseCodecName( L"lz4" );
    CHECK( !d.has_value() && d.error() == errc::unknown_codec );
}

void FindCodecMatchesBuildFlags()
{
    // R18: FindCodec(CodecId::None) is documented to return
    // std::errc::invalid_argument (see codec.h), not merely "an error".
    auto none = FindCodec( CodecId::None );
    CHECK(
        !none.has_value()
        && none.error()
            == std::make_error_code( std::errc::invalid_argument ) );

    auto z = FindCodec( CodecId::Zstd );
#ifdef RCEDIT_HAS_ZSTD
    CHECK(
        z.has_value() && ( *z )->Id() == CodecId::Zstd
        && ( *z )->Name() == L"zstd" );
    CHECK( IsCodecAvailable( CodecId::Zstd ) );
#else
    CHECK( !z.has_value() && z.error() == errc::codec_disabled );
    CHECK( !IsCodecAvailable( CodecId::Zstd ) );
#endif

    auto s = FindCodec( CodecId::SevenZip );
#ifdef RCEDIT_HAS_7Z
    CHECK(
        s.has_value() && ( *s )->Id() == CodecId::SevenZip
        && ( *s )->Name() == L"7z" );
    CHECK( IsCodecAvailable( CodecId::SevenZip ) );
#else
    CHECK( !s.has_value() && s.error() == errc::codec_disabled );
    CHECK( !IsCodecAvailable( CodecId::SevenZip ) );
#endif
}

void AvailableNamesListOnlyCompiledCodecs()
{
    const auto names = AvailableCodecNames();
    CHECK( !names.empty() && names.front() == L"none" );
    const bool hasZstd = std::ranges::find( names, L"zstd" ) != names.end();
    const bool has7z = std::ranges::find( names, L"7z" ) != names.end();
#ifdef RCEDIT_HAS_ZSTD
    CHECK( hasZstd );
#else
    CHECK( !hasZstd );
#endif
#ifdef RCEDIT_HAS_7Z
    CHECK( has7z );
#else
    CHECK( !has7z );
#endif
}

// Shared by the zstd and 7z cases below.
void RoundTrip( Codec& codec )
{
    for( const size_t size :
         { size_t( 1 ), size_t( 100 ), size_t( 1u << 20 ) } ) {
        const auto input = Pattern( size );
        std::vector< uint8_t > packed;
        CHECK_EC_OK( codec.Compress( input, packed ) );
        CHECK( !packed.empty() );
        CHECK( DetectCodec( packed ) == codec.Id() );

        const auto content = codec.ContentSize( packed );
        CHECK( content.has_value() && *content == size );

        std::vector< uint8_t > unpacked;
        CHECK_EC_OK( codec.Decompress( packed, unpacked ) );
        CHECK( unpacked == input );
    }
}

void RejectsEmptyInput( Codec& codec )
{
    std::vector< uint8_t > out;
    const auto ec = codec.Compress( std::span< const uint8_t >(), out );
    CHECK( ec == errc::empty_payload );
}

void RejectsCorruptInput( Codec& codec )
{
    const auto input = Pattern( 1000 );
    std::vector< uint8_t > packed;
    CHECK_EC_OK( codec.Compress( input, packed ) );
    // Keep the magic, trash the rest.
    for( size_t i = 8; i < packed.size(); ++i ) {
        packed[ i ] = static_cast< uint8_t >( ~packed[ i ] );
    }

    std::vector< uint8_t > out;
    const auto ec = codec.Decompress( packed, out );
    CHECK( static_cast< bool >( ec ) );
}

#ifdef RCEDIT_HAS_ZSTD
void ZstdRoundTrip()
{
    RoundTrip( ZstdCodec() );
}
void ZstdRejectsEmpty()
{
    RejectsEmptyInput( ZstdCodec() );
}
void ZstdRejectsCorrupt()
{
    RejectsCorruptInput( ZstdCodec() );
}

// Builds a valid zstd frame that omits the embedded content size.
// ZstdCodecImpl::Compress always goes through ZSTD_compress2, which pledges
// the size and so always produces a frame with a known content size; the
// only way to reach the codec's "unknown content size" decompression branch
// is to feed it a frame built like this one. Note: neither leaving the
// pledged size unset nor explicitly pledging ZSTD_CONTENTSIZE_UNKNOWN is
// sufficient here — zstd's internal representation of "unknown" for the
// pledge (pledgedSrcSize+1 wrapping to 0) is indistinguishable from "no
// pledge given", and with the *entire* input handed over in a single
// ZSTD_e_end call zstd auto-infers and writes the size anyway. The only
// reliable way to suppress the header field is ZSTD_c_contentSizeFlag=0.
std::vector< uint8_t > CompressStreamingNoContentSize(
    std::span< const uint8_t > input )
{
    struct CCtxDeleter
    {
        void operator()( ZSTD_CCtx* p ) const noexcept { ZSTD_freeCCtx( p ); }
    };
    std::unique_ptr< ZSTD_CCtx, CCtxDeleter > cctx( ZSTD_createCCtx() );
    CHECK( cctx != nullptr );

    const size_t flagRc =
        ZSTD_CCtx_setParameter( cctx.get(), ZSTD_c_contentSizeFlag, 0 );
    CHECK( !ZSTD_isError( flagRc ) );

    std::vector< uint8_t > out( ZSTD_compressBound( input.size() ) + 64 );
    ZSTD_inBuffer in{ input.data(), input.size(), 0 };
    ZSTD_outBuffer outBuf{ out.data(), out.size(), 0 };
    const size_t rc =
        ZSTD_compressStream2( cctx.get(), &outBuf, &in, ZSTD_e_end );
    CHECK( !ZSTD_isError( rc ) );
    CHECK( rc == 0 );  // fully flushed in this single call
    out.resize( outBuf.pos );
    return out;
}

void ZstdStreamingFrameRoundTrip()
{
    const auto input = Pattern( 5000 );
    const auto packed = CompressStreamingNoContentSize( input );
    CHECK( !packed.empty() );
    CHECK( DetectCodec( packed ) == CodecId::Zstd );

    // No embedded content size: ContentSize() must report unknown, which is
    // what routes Decompress() through the streaming branch under test.
    CHECK( !ZstdCodec().ContentSize( packed ).has_value() );

    std::vector< uint8_t > unpacked;
    CHECK_EC_OK( ZstdCodec().Decompress( packed, unpacked ) );
    CHECK( unpacked == input );
}

void ZstdStreamingFrameRejectsTruncation()
{
    const auto input = Pattern( 5000 );
    const auto packed = CompressStreamingNoContentSize( input );
    CHECK( !ZstdCodec().ContentSize( packed ).has_value() );
    CHECK( packed.size() > 100 );

    // Cut the frame well before its end. ZSTD_decompressStream runs out of
    // input while it still expects more (a non-zero, non-error return); the
    // codec must surface that as corrupt_payload, not a silently short
    // decompression.
    std::vector< uint8_t > truncated(
        packed.begin(),
        packed.begin() + static_cast< ptrdiff_t >( packed.size() / 2 ) );

    std::vector< uint8_t > out;
    const auto ec = ZstdCodec().Decompress( truncated, out );
    CHECK( ec == errc::corrupt_payload );
}
#endif

#ifdef RCEDIT_HAS_7Z
void SevenZipRoundTrip()
{
    RoundTrip( SevenZipCodec() );
}
void SevenZipRejectsEmpty()
{
    RejectsEmptyInput( SevenZipCodec() );
}
void SevenZipRejectsCorrupt()
{
    RejectsCorruptInput( SevenZipCodec() );
}
#endif

constexpr TestCase kCases[] = {
    { "DetectsMagics", DetectsMagics },
    { "NamesRoundTrip", NamesRoundTrip },
    { "FindCodecMatchesBuildFlags", FindCodecMatchesBuildFlags },
    { "AvailableNamesListOnlyCompiledCodecs",
      AvailableNamesListOnlyCompiledCodecs },
#ifdef RCEDIT_HAS_ZSTD
    { "ZstdRoundTrip", ZstdRoundTrip },
    { "ZstdRejectsEmpty", ZstdRejectsEmpty },
    { "ZstdRejectsCorrupt", ZstdRejectsCorrupt },
    { "ZstdStreamingFrameRoundTrip", ZstdStreamingFrameRoundTrip },
    { "ZstdStreamingFrameRejectsTruncation",
      ZstdStreamingFrameRejectsTruncation },
#endif
#ifdef RCEDIT_HAS_7Z
    { "SevenZipRoundTrip", SevenZipRoundTrip },
    { "SevenZipRejectsEmpty", SevenZipRejectsEmpty },
    { "SevenZipRejectsCorrupt", SevenZipRejectsCorrupt },
#endif
};

}  // namespace

int RunCodecTests()
{
    return RunGroup( "codec", kCases );
}

}  // namespace rcedit::test
