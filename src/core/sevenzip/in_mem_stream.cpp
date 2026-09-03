//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/in_mem_stream.h"

#include <algorithm>
#include <cstring>

namespace rcedit::sevenzip {

InMemStream::InMemStream( std::span< const uint8_t > buffer ) noexcept
    : m_buffer( buffer )
{
}

Z7_COM7F_IMF(
    InMemStream::Read( void* data, UInt32 size, UInt32* processedSize ) )
{
    if( processedSize != nullptr ) {
        *processedSize = 0;
    }
    if( data == nullptr ) {
        return E_POINTER;
    }
    if( size == 0 || m_pos >= m_buffer.size() ) {
        return S_OK;
    }

    const size_t available = m_buffer.size() - m_pos;
    const size_t count = std::min< size_t >( size, available );
    std::memcpy( data, m_buffer.data() + m_pos, count );
    m_pos += count;
    if( processedSize != nullptr ) {
        *processedSize = static_cast< UInt32 >( count );
    }
    return S_OK;
}

Z7_COM7F_IMF(
    InMemStream::Seek( Int64 offset, UInt32 seekOrigin, UInt64* newPosition ) )
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

}  // namespace rcedit::sevenzip
