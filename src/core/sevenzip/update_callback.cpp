//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/update_callback.h"

#include "core/sevenzip/in_mem_stream.h"

namespace rcedit::sevenzip {

UpdateCallback::UpdateCallback(
    std::span< const uint8_t > content,
    std::wstring name )
    : m_content( content )
    , m_name( std::move( name ) )
{
    ::GetSystemTimeAsFileTime( &m_time );
}

Z7_COM7F_IMF( UpdateCallback::SetTotal( UInt64 /*size*/ ) )
{
    return S_OK;
}

Z7_COM7F_IMF( UpdateCallback::SetCompleted( const UInt64* /*completeValue*/ ) )
{
    return S_OK;
}

Z7_COM7F_IMF(
    UpdateCallback::GetUpdateItemInfo(
        UInt32 /*index*/,
        Int32* newData,
        Int32* newProperties,
        UInt32* indexInArchive ) )
{
    if( newData != nullptr ) {
        *newData = 1;
    }

    if( newProperties != nullptr ) {
        *newProperties = 1;
    }

    if( indexInArchive != nullptr ) {
        *indexInArchive = static_cast< UInt32 >( -1 );
    }

    return S_OK;
}

Z7_COM7F_IMF(
    UpdateCallback::GetProperty(
        UInt32 index,
        PROPID propID,
        PROPVARIANT* value ) )
{
    if( index != 0 ) {
        return E_INVALIDARG;
    }

    // Invoked by 7-Zip across a COM frame; no exception may escape.
    try {
        NWindows::NCOM::CPropVariant prop;
        switch( propID ) {
            case kpidPath:
                prop = m_name.c_str();
                break;
            case kpidIsDir:
                prop = false;
                break;
            case kpidIsAnti:
                prop = false;
                break;
            case kpidSize:
                prop = static_cast< UInt64 >( m_content.size() );
                break;
            case kpidAttrib:
                prop = static_cast< UInt32 >( FILE_ATTRIBUTE_NORMAL );
                break;
            case kpidCTime:
            case kpidATime:
            case kpidMTime:
                prop = m_time;
                break;
            default:
                break;
        }

        prop.Detach( value );
    }
    catch( ... ) {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

Z7_COM7F_IMF(
    UpdateCallback::GetStream( UInt32 index, ISequentialInStream** inStream ) )
{
    if( inStream == nullptr ) {
        return E_POINTER;
    }

    *inStream = nullptr;
    if( index != 0 ) {
        return E_INVALIDARG;
    }

    if( m_content.empty() ) {
        return S_OK;  // 7-Zip expects a null stream for empty files
    }

    // Invoked by 7-Zip across a COM frame; no exception may escape.
    try {
        CMyComPtr< ISequentialInStream > stream = new InMemStream( m_content );
        *inStream = stream.Detach();
    }
    catch( ... ) {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

Z7_COM7F_IMF( UpdateCallback::SetOperationResult( Int32 operationResult ) )
{
    if( operationResult != NArchive::NUpdate::NOperationResult::kOK ) {
        m_failed = true;
    }

    return S_OK;
}

}  // namespace rcedit::sevenzip
