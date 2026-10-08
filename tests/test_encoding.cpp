//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include "core/encoding.h"

namespace rcedit::test {

namespace {

void RoundTripsAscii()
{
    auto utf8 = Utf16ToUtf8( L"hello" );
    CHECK( utf8.has_value() );
    CHECK( *utf8 == "hello" );
    auto back = Utf8ToUtf16( *utf8 );
    CHECK( back.has_value() );
    CHECK( *back == L"hello" );
}

void RoundTripsNonAscii()
{
    const std::wstring w = L"café 日本";
    auto utf8 = Utf16ToUtf8( w );
    CHECK( utf8.has_value() );
    CHECK( utf8->size() == 4 + 1 + 1 + 6 );
    auto back = Utf8ToUtf16( *utf8 );
    CHECK( back.has_value() );
    CHECK( *back == w );
}

void EmptyIsEmpty()
{
    auto a = Utf16ToUtf8( L"" );
    CHECK( a.has_value() && a->empty() );
    auto b = Utf8ToUtf16( "" );
    CHECK( b.has_value() && b->empty() );
}

void RejectsInvalidUtf8()
{
    const char bad[] = { static_cast< char >( 0xC3 ),
                         static_cast< char >( 0x28 ),
                         0 };
    auto r = Utf8ToUtf16( bad );
    CHECK( !r.has_value() );
}

void RejectsLoneSurrogate()
{
    const wchar_t bad[] = { 0xD800, 0 };
    auto r = Utf16ToUtf8( bad );
    CHECK( !r.has_value() );
}

void EqualsIgnoreCaseComparesFolded()
{
    CHECK( EqualsIgnoreCase( L"hello", L"hello" ) );
    CHECK( EqualsIgnoreCase( L"HELLO", L"hello" ) );
    CHECK( EqualsIgnoreCase( L"HeLLo", L"hEllO" ) );
    CHECK( !EqualsIgnoreCase( L"hello", L"world" ) );
    CHECK( !EqualsIgnoreCase( L"hello", L"hell" ) );
    CHECK( !EqualsIgnoreCase( L"hell", L"hello" ) );
    CHECK( EqualsIgnoreCase( L"", L"" ) );
    CHECK( !EqualsIgnoreCase( L"", L"x" ) );
    // Non-ASCII: towlower in the default "C" locale does not fold accented
    // letters, so only require that equal non-ASCII strings compare equal.
    CHECK( EqualsIgnoreCase( L"café", L"café" ) );
}

constexpr TestCase kCases[] = {
    { "RoundTripsAscii", RoundTripsAscii },
    { "RoundTripsNonAscii", RoundTripsNonAscii },
    { "EmptyIsEmpty", EmptyIsEmpty },
    { "RejectsInvalidUtf8", RejectsInvalidUtf8 },
    { "RejectsLoneSurrogate", RejectsLoneSurrogate },
    { "EqualsIgnoreCaseComparesFolded", EqualsIgnoreCaseComparesFolded },
};

}  // namespace

int RunEncodingTests()
{
    return RunGroup( "encoding", kCases );
}

}  // namespace rcedit::test
