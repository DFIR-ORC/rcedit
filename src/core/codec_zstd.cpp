//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/codec_zstd.h"

#include <memory>
#include <new>

#include <zstd.h>

#include "core/encoding.h"
#include "core/error.h"
#include "core/log.h"

namespace rcedit {

namespace {

struct CCtxDeleter
{
    void operator()( ZSTD_CCtx* p ) const noexcept { ZSTD_freeCCtx( p ); }
};
struct DCtxDeleter
{
    void operator()( ZSTD_DCtx* p ) const noexcept { ZSTD_freeDCtx( p ); }
};

std::wstring ZstdErrorName( size_t code )
{
    return AnsiToUtf16( ZSTD_getErrorName( code ) );
}

}  // namespace

std::error_code ZstdCodecImpl::Compress(
    std::span< const uint8_t > input,
    std::vector< uint8_t >& output )
{
    if( input.empty() ) {
        return make_error_code( errc::empty_payload );
    }

    std::unique_ptr< ZSTD_CCtx, CCtxDeleter > cctx( ZSTD_createCCtx() );
    if( !cctx ) {
        return std::make_error_code( std::errc::not_enough_memory );
    }

    const size_t levelResult =
        ZSTD_CCtx_setParameter( cctx.get(), ZSTD_c_compressionLevel, 19 );
    if( ZSTD_isError( levelResult ) ) {
        Log::Debug(
            L"Failed ZSTD_CCtx_setParameter level [{}]",
            ZstdErrorName( levelResult ) );
        return std::make_error_code( std::errc::invalid_argument );
    }
    // R14: rcedit produces every frame it later reads back (payloads round-trip
    // through this codec only), so the checksum is pure defense-in-depth: it
    // catches accidental corruption in transit/storage and is verified
    // transparently by ZSTD_decompressDCtx/ZSTD_decompressStream below.
    const size_t checksumResult =
        ZSTD_CCtx_setParameter( cctx.get(), ZSTD_c_checksumFlag, 1 );
    if( ZSTD_isError( checksumResult ) ) {
        Log::Debug(
            L"Failed ZSTD_CCtx_setParameter checksum [{}]",
            ZstdErrorName( checksumResult ) );
        return std::make_error_code( std::errc::invalid_argument );
    }

    const size_t bound = ZSTD_compressBound( input.size() );
    if( ZSTD_isError( bound ) ) {
        return std::make_error_code( std::errc::value_too_large );
    }

    try {
        output.resize( bound );
    }
    catch( const std::bad_alloc& ) {
        return std::make_error_code( std::errc::not_enough_memory );
    }

    const size_t written = ZSTD_compress2(
        cctx.get(), output.data(), output.size(), input.data(), input.size() );
    if( ZSTD_isError( written ) ) {
        Log::Debug( L"Failed ZSTD_compress2 [{}]", ZstdErrorName( written ) );
        output.clear();
        return std::make_error_code( std::errc::io_error );
    }

    output.resize( written );
    return {};
}

std::optional< uint64_t > ZstdCodecImpl::ContentSize(
    std::span< const uint8_t > input )
{
    const unsigned long long size =
        ZSTD_getFrameContentSize( input.data(), input.size() );
    if( size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN ) {
        return std::nullopt;
    }
    return static_cast< uint64_t >( size );
}

std::error_code ZstdCodecImpl::Decompress(
    std::span< const uint8_t > input,
    std::vector< uint8_t >& output )
{
    if( input.empty() ) {
        return make_error_code( errc::empty_payload );
    }

    std::unique_ptr< ZSTD_DCtx, DCtxDeleter > dctx( ZSTD_createDCtx() );
    if( !dctx ) {
        return std::make_error_code( std::errc::not_enough_memory );
    }

    std::vector< uint8_t > result;

    if( const auto known = ContentSize( input ) ) {
        try {
            result.resize( static_cast< size_t >( *known ) );
        }
        catch( const std::bad_alloc& ) {
            return std::make_error_code( std::errc::not_enough_memory );
        }

        const size_t actual = ZSTD_decompressDCtx(
            dctx.get(),
            result.data(),
            result.size(),
            input.data(),
            input.size() );
        if( ZSTD_isError( actual ) ) {
            Log::Debug(
                L"Failed ZSTD_decompressDCtx [{}]", ZstdErrorName( actual ) );
            return make_error_code( errc::corrupt_payload );
        }
        result.resize( actual );
        output = std::move( result );
        return {};
    }

    // Frame without a recorded content size: streaming loop. Track the last
    // ZSTD_decompressStream return: it is a hint (not an error caught by
    // ZSTD_isError) that is 0 only once the frame is fully reconstructed, and
    // > 0 while more input is still expected. Exiting the loop because input
    // ran out (in.pos == in.size) with a non-zero last return means the frame
    // was truncated, which must be reported as corrupt, not as a silently
    // short decompression.
    ZSTD_inBuffer in{ input.data(), input.size(), 0 };
    std::vector< uint8_t > chunk( ZSTD_DStreamOutSize() );
    // Non-zero sentinel: input is non-empty (checked above), so the loop
    // below always runs at least once and overwrites this before use.
    size_t rc = 1;
    while( in.pos < in.size ) {
        ZSTD_outBuffer out{ chunk.data(), chunk.size(), 0 };
        rc = ZSTD_decompressStream( dctx.get(), &out, &in );
        if( ZSTD_isError( rc ) ) {
            Log::Debug(
                L"Failed ZSTD_decompressStream [{}]", ZstdErrorName( rc ) );
            return make_error_code( errc::corrupt_payload );
        }
        try {
            result.insert(
                result.end(),
                chunk.begin(),
                chunk.begin() + static_cast< ptrdiff_t >( out.pos ) );
        }
        catch( const std::bad_alloc& ) {
            return std::make_error_code( std::errc::not_enough_memory );
        }
        if( rc == 0 ) {
            break;
        }
    }
    if( rc != 0 ) {
        Log::Debug( L"Truncated zstd frame: input exhausted before frame end" );
        return make_error_code( errc::corrupt_payload );
    }

    output = std::move( result );
    return {};
}

Codec& ZstdCodec()
{
    static ZstdCodecImpl codec;
    return codec;
}

}  // namespace rcedit
