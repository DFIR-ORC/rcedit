//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/out_mem_stream.h"

#include <cstring>
#include <exception>

namespace rcedit::sevenzip {

OutMemStream::OutMemStream( std::vector< uint8_t >& buffer ) noexcept
    : m_buffer( buffer )
{
}

Z7_COM7F_IMF(
    OutMemStream::Write(
        const void* data,
        UInt32 size,
        UInt32* processedSize ) )
{
    if( processedSize != nullptr ) {
        *processedSize = 0;
    }
    if( data == nullptr ) {
        return E_POINTER;
    }
    if( size == 0 ) {
        return S_OK;
    }

    const size_t required = m_pos + size;
    try {
        if( m_buffer.size() < required ) {
            m_buffer.resize( required );
        }
    }
    catch( const std::exception& ) {
        // Also catches std::length_error: a corrupt/malicious archive can
        // declare an unpacked size past vector::max_size(), which resize()
        // reports via length_error rather than bad_alloc. Neither may cross
        // the 7-Zip COM / library boundary.
        return E_OUTOFMEMORY;
    }

    std::memcpy( m_buffer.data() + m_pos, data, size );
    m_pos += size;
    if( processedSize != nullptr ) {
        *processedSize = size;
    }
    return S_OK;
}

Z7_COM7F_IMF(
    OutMemStream::Seek( Int64 offset, UInt32 seekOrigin, UInt64* newPosition ) )
{
    size_t pos = 0;
    const HRESULT hr =
        ComputeSeekPosition( offset, seekOrigin, m_pos, m_buffer.size(), pos );
    if( FAILED( hr ) ) {
        return hr;
    }

    m_pos = pos;
    if( newPosition != nullptr ) {
        *newPosition = static_cast< UInt64 >( m_pos );
    }
    return S_OK;
}

Z7_COM7F_IMF( OutMemStream::SetSize( UInt64 newSize ) )
{
    try {
        m_buffer.resize( static_cast< size_t >( newSize ) );
    }
    catch( const std::exception& ) {
        // Also catches std::length_error: a corrupt/malicious archive can
        // declare an unpacked size past vector::max_size(), which resize()
        // reports via length_error rather than bad_alloc. Neither may cross
        // the 7-Zip COM / library boundary.
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

}  // namespace rcedit::sevenzip
