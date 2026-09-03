//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/encoding.h"

#include <windows.h>

#include <cwctype>

#include "core/error.h"

namespace rcedit {

std::expected< std::string, std::error_code > Utf16ToUtf8(
    std::wstring_view input )
{
    if( input.empty() ) {
        return std::string();
    }

    const int size = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        input.data(),
        static_cast< int >( input.size() ),
        nullptr,
        0,
        nullptr,
        nullptr );
    if( size <= 0 ) {
        return std::unexpected( LastWin32Error() );
    }

    std::string out( static_cast< size_t >( size ), '\0' );
    const int written = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        input.data(),
        static_cast< int >( input.size() ),
        out.data(),
        size,
        nullptr,
        nullptr );
    if( written != size ) {
        return std::unexpected( LastWin32Error() );
    }

    return out;
}

std::expected< std::wstring, std::error_code > Utf8ToUtf16(
    std::string_view input )
{
    if( input.empty() ) {
        return std::wstring();
    }

    const int size = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        input.data(),
        static_cast< int >( input.size() ),
        nullptr,
        0 );
    if( size <= 0 ) {
        return std::unexpected( LastWin32Error() );
    }

    std::wstring out( static_cast< size_t >( size ), L'\0' );
    const int written = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        input.data(),
        static_cast< int >( input.size() ),
        out.data(),
        size );
    if( written != size ) {
        return std::unexpected( LastWin32Error() );
    }

    return out;
}

std::wstring AnsiToUtf16( std::string_view input )
{
    if( input.empty() ) {
        return {};
    }

    const int size = ::MultiByteToWideChar(
        CP_ACP,
        0,
        input.data(),
        static_cast< int >( input.size() ),
        nullptr,
        0 );
    if( size <= 0 ) {
        return L"<unconvertible>";
    }

    std::wstring out( static_cast< size_t >( size ), L'\0' );
    ::MultiByteToWideChar(
        CP_ACP,
        0,
        input.data(),
        static_cast< int >( input.size() ),
        out.data(),
        size );
    return out;
}

bool EqualsIgnoreCase( std::wstring_view a, std::wstring_view b ) noexcept
{
    if( a.size() != b.size() ) {
        return false;
    }

    for( size_t i = 0; i < a.size(); ++i ) {
        if( std::towlower( static_cast< wint_t >( a[ i ] ) )
            != std::towlower( static_cast< wint_t >( b[ i ] ) ) ) {
            return false;
        }
    }

    return true;
}

}  // namespace rcedit
