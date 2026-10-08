//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include <windows.h>

#include "core/error.h"
#include "core/format.h"

namespace rcedit::test {

namespace {

void ErrcHasCategoryAndMessage()
{
    std::error_code ec = errc::codec_disabled;
    CHECK( ec.category() == rcedit_category() );
    CHECK( std::string_view( ec.category().name() ) == "rcedit" );
    CHECK( ec.message() == "codec disabled in this build" );
    CHECK( ec == errc::codec_disabled );
    CHECK( ec != errc::unknown_codec );
}

void EveryErrcHasDistinctMessage()
{
    const errc all[] = {
        errc::invalid_identifier, errc::invalid_language,
        errc::unknown_codec,      errc::codec_disabled,
        errc::resource_not_found, errc::ambiguous_language,
        errc::read_only,          errc::self_update,
        errc::corrupt_payload,    errc::empty_payload,
    };
    for( size_t i = 0; i < std::size( all ); ++i ) {
        const std::error_code a = all[ i ];
        CHECK( !a.message().empty() );
        CHECK( a.message() != "unknown error" );
        for( size_t j = i + 1; j < std::size( all ); ++j ) {
            const std::error_code b = all[ j ];
            CHECK( a.message() != b.message() );
        }
    }
}

void Win32ErrorUsesSystemCategory()
{
    const std::error_code ec = Win32Error( ERROR_FILE_NOT_FOUND );
    CHECK( ec.category() == std::system_category() );
    CHECK( ec.value() == ERROR_FILE_NOT_FOUND );

    ::SetLastError( ERROR_ACCESS_DENIED );
    const std::error_code last = LastWin32Error();
    CHECK( last.value() == ERROR_ACCESS_DENIED );
}

void HResultErrorKeepsValue()
{
    const std::error_code ec = HResultError( E_FAIL );
    CHECK( ec.category() == std::system_category() );
    CHECK( ec.value() == E_FAIL );
}

void FormatErrorShowsCategoryAndMessage()
{
    const std::wstring s = FormatError( std::error_code( errc::read_only ) );
    CHECK( s.find( L"rcedit" ) != std::wstring::npos );
    CHECK( s.find( L"read-only" ) != std::wstring::npos );

    const std::wstring w = FormatError( Win32Error( ERROR_FILE_NOT_FOUND ) );
    CHECK( w.find( L"0x2" ) != std::wstring::npos );
}

constexpr TestCase kCases[] = {
    { "ErrcHasCategoryAndMessage", ErrcHasCategoryAndMessage },
    { "EveryErrcHasDistinctMessage", EveryErrcHasDistinctMessage },
    { "Win32ErrorUsesSystemCategory", Win32ErrorUsesSystemCategory },
    { "HResultErrorKeepsValue", HResultErrorKeepsValue },
    { "FormatErrorShowsCategoryAndMessage",
      FormatErrorShowsCategoryAndMessage },
};

}  // namespace

int RunErrorTests()
{
    return RunGroup( "error", kCases );
}

}  // namespace rcedit::test
