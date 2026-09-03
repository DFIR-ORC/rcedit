//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstddef>
#include <cstdint>

// The project defines WIN32_LEAN_AND_MEAN globally (guard.h / root
// CMakeLists.txt), which excludes <ole2.h> from <windows.h> and with it
// PROPVARIANT/PROPID and the rest of the OLE/COM types 7-Zip's interfaces
// are built on. Undefine it before pulling in the 7-Zip headers (the first
// <windows.h> inclusion in any translation unit that reaches this header)
// so the full COM surface is available; this is scoped to this translation
// unit only and does not weaken WIN32_LEAN_AND_MEAN for the rest of rcedit.
#ifdef WIN32_LEAN_AND_MEAN
#    undef WIN32_LEAN_AND_MEAN
#endif

// 7-Zip headers are not /W4 clean.
#pragma warning( push, 0 )
#include <7zip/7zip.h>
#include <7zip/extras.h>
#pragma warning( pop )

namespace rcedit::sevenzip {

// Static 7-Zip needs explicit registration of formats and codecs. Idempotent.
inline void EnsureInitialized()
{
#ifdef _7ZIP_STATIC
    static const bool initialized = [] {
        ::lib7zCrcTableInit();
        NArchive::N7z::Register();
        NCompress::RegisterCodecCopy();
        NCompress::NBcj::RegisterCodecBCJ();
        NCompress::NBcj2::RegisterCodecBCJ2();
        NCompress::NLzma::RegisterCodecLZMA();
        NCompress::NLzma2::RegisterCodecLZMA2();
        return true;
    }();
    (void)initialized;
#endif
}

// R8: InMemStream::Seek and OutMemStream::Seek need identical seek math
// (only the notion of "current size" differs). Shared here instead of
// duplicating the switch/overflow logic in both .cpp files.
//
// R4: the brief's HRESULT_WIN32_ERROR_NEGATIVE_SEEK is not a real macro;
// use HRESULT_FROM_WIN32(ERROR_NEGATIVE_SEEK) directly.
[[nodiscard]] inline HRESULT ComputeSeekPosition(
    Int64 offset,
    UInt32 seekOrigin,
    size_t currentPos,
    size_t size,
    size_t& outPos ) noexcept
{
    Int64 base = 0;
    switch( seekOrigin ) {
        case STREAM_SEEK_SET:
            base = 0;
            break;
        case STREAM_SEEK_CUR:
            base = static_cast< Int64 >( currentPos );
            break;
        case STREAM_SEEK_END:
            base = static_cast< Int64 >( size );
            break;
        default:
            return STG_E_INVALIDFUNCTION;
    }

    const Int64 target = base + offset;
    if( target < 0 ) {
        return HRESULT_FROM_WIN32( ERROR_NEGATIVE_SEEK );
    }

    outPos = static_cast< size_t >( target );
    return S_OK;
}

}  // namespace rcedit::sevenzip
