//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/codec_7z.h"

#include "core/error.h"
#include "core/format.h"
#include "core/log.h"
#include "core/sevenzip/extract_callback.h"
#include "core/sevenzip/in_mem_stream.h"
#include "core/sevenzip/out_mem_stream.h"
#include "core/sevenzip/sevenzip.h"
#include "core/sevenzip/update_callback.h"

namespace rcedit {

namespace {

// R9: Codec::Compress has no entry-name parameter (the abstract interface
// carries only input/output buffers), so the single archive entry always
// uses this fixed internal name. This is an intentional deviation from the
// design spec's "entry named after the resource name": Decompress always
// reads back archive item index 0, so the entry name never surfaces to
// users and does not need to vary.
constexpr const wchar_t* kEntryName = L"payload";
constexpr UInt32 kLevel = 9;

// Opens a 7z archive from memory. Returns a null pointer on failure.
CMyComPtr< IInArchive > OpenArchive(
    std::span< const uint8_t > input,
    std::error_code& ec )
{
    sevenzip::EnsureInitialized();

    CMyComPtr< IInArchive > archive;
    HRESULT hr = ::CreateObject(
        &CLSID_CFormat7z,
        &IID_IInArchive,
        reinterpret_cast< void** >( &archive ) );
    if( FAILED( hr ) || !archive ) {
        Log::Debug(
            L"Failed CreateObject(IInArchive) [{}]",
            FormatError( HResultError( hr ) ) );
        ec = HResultError( hr );
        return nullptr;
    }

    CMyComPtr< IInStream > stream = new sevenzip::InMemStream( input );
    hr = archive->Open( stream, nullptr, nullptr );
    if( FAILED( hr ) || hr == S_FALSE ) {
        Log::Debug(
            L"Failed IInArchive::Open [{}]",
            FormatError( HResultError( hr ) ) );
        ec = make_error_code( errc::corrupt_payload );
        return nullptr;
    }

    UInt32 count = 0;
    hr = archive->GetNumberOfItems( &count );
    if( FAILED( hr ) || count != 1 ) {
        Log::Debug( L"Unexpected 7z item count: {}", count );
        ec = make_error_code( errc::corrupt_payload );
        return nullptr;
    }

    ec.clear();
    return archive;
}

}  // namespace

std::error_code SevenZipCodecImpl::Compress(
    std::span< const uint8_t > input,
    std::vector< uint8_t >& output )
{
    if( input.empty() ) {
        return make_error_code( errc::empty_payload );
    }

    sevenzip::EnsureInitialized();

    CMyComPtr< IOutArchive > archive;
    HRESULT hr = ::CreateObject(
        &CLSID_CFormat7z,
        &IID_IOutArchive,
        reinterpret_cast< void** >( &archive ) );
    if( FAILED( hr ) || !archive ) {
        Log::Debug(
            L"Failed CreateObject(IOutArchive) [{}]",
            FormatError( HResultError( hr ) ) );
        return HResultError( hr );
    }

    CMyComPtr< ISetProperties > setProperties;
    hr = archive->QueryInterface(
        IID_ISetProperties, reinterpret_cast< void** >( &setProperties ) );
    if( FAILED( hr ) || !setProperties ) {
        Log::Debug(
            L"Failed QueryInterface(ISetProperties) [{}]",
            FormatError( HResultError( hr ) ) );
        return HResultError( hr );
    }

    const wchar_t* names[] = { L"x" };
    NWindows::NCOM::CPropVariant values[] = { kLevel };
    hr = setProperties->SetProperties( names, values, 1 );
    if( FAILED( hr ) ) {
        Log::Debug(
            L"Failed ISetProperties::SetProperties [{}]",
            FormatError( HResultError( hr ) ) );
        return HResultError( hr );
    }

    output.clear();
    CMyComPtr< IOutStream > outStream = new sevenzip::OutMemStream( output );
    sevenzip::UpdateCallback* callbackImpl =
        new sevenzip::UpdateCallback( input, kEntryName );
    CMyComPtr< IArchiveUpdateCallback > callback = callbackImpl;

    hr = archive->UpdateItems( outStream, 1, callback );
    if( FAILED( hr ) ) {
        Log::Debug(
            L"Failed IOutArchive::UpdateItems [{}]",
            FormatError( HResultError( hr ) ) );
        output.clear();
        return HResultError( hr );
    }
    if( callbackImpl->Failed() ) {
        output.clear();
        return std::make_error_code( std::errc::io_error );
    }

    return {};
}

std::optional< uint64_t > SevenZipCodecImpl::ContentSize(
    std::span< const uint8_t > input )
{
    std::error_code ec;
    CMyComPtr< IInArchive > archive = OpenArchive( input, ec );
    if( ec ) {
        return std::nullopt;
    }

    NWindows::NCOM::CPropVariant prop;
    const HRESULT hr = archive->GetProperty( 0, kpidSize, &prop );
    archive->Close();
    if( FAILED( hr ) || prop.vt != VT_UI8 ) {
        return std::nullopt;
    }
    return static_cast< uint64_t >( prop.uhVal.QuadPart );
}

std::error_code SevenZipCodecImpl::Decompress(
    std::span< const uint8_t > input,
    std::vector< uint8_t >& output )
{
    if( input.empty() ) {
        return make_error_code( errc::empty_payload );
    }

    std::error_code ec;
    CMyComPtr< IInArchive > archive = OpenArchive( input, ec );
    if( ec ) {
        return ec;
    }

    std::vector< uint8_t > result;
    sevenzip::ExtractCallback* callbackImpl =
        new sevenzip::ExtractCallback( result );
    CMyComPtr< IArchiveExtractCallback > callback = callbackImpl;

    const UInt32 index = 0;
    const HRESULT hr = archive->Extract( &index, 1, 0, callback );
    archive->Close();
    if( FAILED( hr ) ) {
        Log::Debug(
            L"Failed IInArchive::Extract [{}]",
            FormatError( HResultError( hr ) ) );
        return make_error_code( errc::corrupt_payload );
    }
    if( callbackImpl->Failed() ) {
        return make_error_code( errc::corrupt_payload );
    }

    output = std::move( result );
    return {};
}

Codec& SevenZipCodec()
{
    static SevenZipCodecImpl codec;
    return codec;
}

}  // namespace rcedit
