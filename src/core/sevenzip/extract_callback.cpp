//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/extract_callback.h"

#include "core/sevenzip/out_mem_stream.h"

namespace rcedit::sevenzip {

ExtractCallback::ExtractCallback( std::vector< uint8_t >& output ) noexcept
    : m_output( output )
{
}

Z7_COM7F_IMF( ExtractCallback::SetTotal( UInt64 /*size*/ ) )
{
    return S_OK;
}

Z7_COM7F_IMF( ExtractCallback::SetCompleted( const UInt64* /*completeValue*/ ) )
{
    return S_OK;
}

Z7_COM7F_IMF(
    ExtractCallback::GetStream(
        UInt32 index,
        ISequentialOutStream** outStream,
        Int32 askExtractMode ) )
{
    if( outStream == nullptr ) {
        return E_POINTER;
    }

    *outStream = nullptr;

    if( index != 0
        || askExtractMode != NArchive::NExtract::NAskMode::kExtract ) {
        return S_OK;
    }

    // Invoked by 7-Zip across a COM frame; no exception may escape.
    try {
        m_output.clear();
        CMyComPtr< ISequentialOutStream > stream = new OutMemStream( m_output );
        *outStream = stream.Detach();
    }
    catch( ... ) {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

Z7_COM7F_IMF( ExtractCallback::PrepareOperation( Int32 /*askExtractMode*/ ) )
{
    return S_OK;
}

Z7_COM7F_IMF( ExtractCallback::SetOperationResult( Int32 operationResult ) )
{
    if( operationResult != NArchive::NExtract::NOperationResult::kOK ) {
        m_failed = true;
    }

    return S_OK;
}

}  // namespace rcedit::sevenzip
