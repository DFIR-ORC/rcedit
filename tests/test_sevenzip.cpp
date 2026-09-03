//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <vector>

#include "core/sevenzip/out_mem_stream.h"

namespace rcedit::test {

namespace {

// A corrupt/malicious 7z archive can declare an unpacked size far past
// std::vector<uint8_t>::max_size(). During extraction 7-Zip forwards that raw
// UInt64 straight to IOutStream::SetSize, where vector::resize() throws
// std::length_error (not std::bad_alloc). That exception must never escape the
// COM / library boundary: OutMemStream must swallow it and return a FAILED
// HRESULT so SevenZipCodecImpl::Decompress can surface a plain std::error_code.
void OutMemStreamSetSizeRejectsOversize()
{
    std::vector< uint8_t > buffer;
    sevenzip::OutMemStream stream( buffer );

    HRESULT hr = S_OK;
    try {
        hr = stream.SetSize( ( std::numeric_limits< UInt64 >::max )() );
    }
    catch( const std::exception& ) {
        // Reaching here means the exception crossed the COM boundary, which
        // is exactly the defect under test.
        CHECK( !"SetSize threw across the COM boundary" );
        return;
    }

    CHECK( FAILED( hr ) );
    CHECK( buffer.empty() );
}

// Same defense on the Write path: seek the cursor near the end of the address
// space so Write()'s implicit grow (m_pos + size) exceeds max_size().
void OutMemStreamWriteRejectsOversize()
{
    std::vector< uint8_t > buffer;
    sevenzip::OutMemStream stream( buffer );

    UInt64 newPos = 0;
    const HRESULT seekHr = stream.Seek(
        ( std::numeric_limits< Int64 >::max )(), STREAM_SEEK_SET, &newPos );
    CHECK( seekHr == S_OK );

    const uint8_t payload = 0x42;
    UInt32 processed = 0xFFFFFFFFu;
    HRESULT hr = S_OK;
    try {
        hr = stream.Write( &payload, 1, &processed );
    }
    catch( const std::exception& ) {
        CHECK( !"Write threw across the COM boundary" );
        return;
    }

    CHECK( FAILED( hr ) );
    CHECK( processed == 0 );
    CHECK( buffer.empty() );
}

constexpr TestCase kCases[] = {
    { "OutMemStreamSetSizeRejectsOversize",
      OutMemStreamSetSizeRejectsOversize },
    { "OutMemStreamWriteRejectsOversize", OutMemStreamWriteRejectsOversize },
};

}  // namespace

int RunSevenZipTests()
{
    return RunGroup( "sevenzip", kCases );
}

}  // namespace rcedit::test
