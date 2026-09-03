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
#include "core/resource_id.h"

namespace rcedit::test {

namespace {

bool IsNumeric( const ResourceId& id, uint16_t expected )
{
    return std::holds_alternative< uint16_t >( id )
        && std::get< uint16_t >( id ) == expected;
}

bool IsString( const ResourceId& id, std::wstring_view expected )
{
    return std::holds_alternative< std::wstring >( id )
        && std::get< std::wstring >( id ) == expected;
}

void ParsesHashNumeric()
{
    auto r = ParseResourceName( L"#101" );
    CHECK( r.has_value() && IsNumeric( *r, 101 ) );
    auto t = ParseResourceType( L"#10" );
    CHECK( t.has_value() && IsNumeric( *t, 10 ) );
}

void BareDecimalIsAString()
{
    auto r = ParseResourceName( L"101" );
    CHECK( r.has_value() && IsString( *r, L"101" ) );
}

void RejectsBadHashForms()
{
    CHECK( !ParseResourceName( L"#" ).has_value() );
    CHECK( !ParseResourceName( L"#0" ).has_value() );
    CHECK( !ParseResourceName( L"#65536" ).has_value() );
    CHECK( !ParseResourceName( L"#12a" ).has_value() );
    CHECK( !ParseResourceName( L"#-1" ).has_value() );
    CHECK( ParseResourceName( L"#" ).error() == errc::invalid_identifier );
}

void RejectsEmpty()
{
    CHECK( !ParseResourceName( L"" ).has_value() );
    CHECK( !ParseResourceType( L"" ).has_value() );
}

void TypeAliasesAreCaseInsensitiveWithOrWithoutPrefix()
{
    auto a = ParseResourceType( L"RT_RCDATA" );
    auto b = ParseResourceType( L"rcdata" );
    auto c = ParseResourceType( L"Rt_RcData" );
    CHECK( a.has_value() && IsNumeric( *a, 10 ) );
    CHECK( b.has_value() && IsNumeric( *b, 10 ) );
    CHECK( c.has_value() && IsNumeric( *c, 10 ) );

    auto m = ParseResourceType( L"MANIFEST" );
    CHECK( m.has_value() && IsNumeric( *m, 24 ) );
    auto gi = ParseResourceType( L"GROUP_ICON" );
    CHECK( gi.has_value() && IsNumeric( *gi, 14 ) );
}

void UnknownTypeIsAString()
{
    auto r = ParseResourceType( L"MYTYPE" );
    CHECK( r.has_value() && IsString( *r, L"MYTYPE" ) );
}

void NameNeverUsesAliases()
{
    auto r = ParseResourceName( L"RCDATA" );
    CHECK( r.has_value() && IsString( *r, L"RCDATA" ) );
}

void FormatsTypesWithAliasOrHash()
{
    CHECK( FormatResourceType( ResourceId( uint16_t( 10 ) ) ) == L"RT_RCDATA" );
    CHECK(
        FormatResourceType( ResourceId( uint16_t( 24 ) ) ) == L"RT_MANIFEST" );
    CHECK( FormatResourceType( ResourceId( uint16_t( 240 ) ) ) == L"#240" );
    CHECK(
        FormatResourceType( ResourceId( std::wstring( L"MYTYPE" ) ) )
        == L"MYTYPE" );
}

void FormatsNamesWithHashOrString()
{
    CHECK( FormatResourceName( ResourceId( uint16_t( 101 ) ) ) == L"#101" );
    CHECK(
        FormatResourceName( ResourceId( std::wstring( L"CONFIG" ) ) )
        == L"CONFIG" );
}

void FormatRoundTrips()
{
    for( const wchar_t* s : { L"#1", L"#65535", L"CONFIG", L"101" } ) {
        auto id = ParseResourceName( s );
        CHECK( id.has_value() );
        CHECK( FormatResourceName( *id ) == s );
    }
    for( const wchar_t* s :
         { L"RT_ICON", L"RT_VERSION", L"#200", L"MYTYPE" } ) {
        auto id = ParseResourceType( s );
        CHECK( id.has_value() );
        CHECK( FormatResourceType( *id ) == s );
    }
}

void ParsesLang()
{
    auto a = ParseLang( L"1033" );
    CHECK( a.has_value() && *a == 1033 );
    auto b = ParseLang( L"0x409" );
    CHECK( b.has_value() && *b == 0x409 );
    auto c = ParseLang( L"0" );
    CHECK( c.has_value() && *c == 0 );
    CHECK( !ParseLang( L"" ).has_value() );
    CHECK( !ParseLang( L"65536" ).has_value() );
    CHECK( !ParseLang( L"-1" ).has_value() );
    CHECK( !ParseLang( L"12x" ).has_value() );
    CHECK( ParseLang( L"x" ).error() == errc::invalid_language );
}

void Win32PointerConversion()
{
    const ResourceId numeric( uint16_t( 10 ) );
    CHECK( ToLpcwstr( numeric ) == MAKEINTRESOURCEW( 10 ) );

    const ResourceId text( std::wstring( L"CONFIG" ) );
    CHECK( std::wstring_view( ToLpcwstr( text ) ) == L"CONFIG" );

    CHECK( IsNumeric( FromLpcwstr( MAKEINTRESOURCEW( 16 ) ), 16 ) );
    CHECK( IsString( FromLpcwstr( L"NAME" ), L"NAME" ) );
}

void KeyEquality()
{
    const ResourceKey a{ ResourceId( uint16_t( 10 ) ),
                         ResourceId( std::wstring( L"X" ) ),
                         0 };
    const ResourceKey b{ ResourceId( uint16_t( 10 ) ),
                         ResourceId( std::wstring( L"X" ) ),
                         0 };
    const ResourceKey c{ ResourceId( uint16_t( 10 ) ),
                         ResourceId( std::wstring( L"X" ) ),
                         std::nullopt };
    CHECK( a == b );
    CHECK( !( a == c ) );
}

void FormatterWorks()
{
    const ResourceId id( std::wstring( L"CONFIG" ) );
    CHECK( std::format( L"{}", id ) == L"CONFIG" );
    const ResourceId n( uint16_t( 7 ) );
    CHECK( std::format( L"{}", n ) == L"#7" );
}

constexpr TestCase kCases[] = {
    { "ParsesHashNumeric", ParsesHashNumeric },
    { "BareDecimalIsAString", BareDecimalIsAString },
    { "RejectsBadHashForms", RejectsBadHashForms },
    { "RejectsEmpty", RejectsEmpty },
    { "TypeAliasesAreCaseInsensitiveWithOrWithoutPrefix",
      TypeAliasesAreCaseInsensitiveWithOrWithoutPrefix },
    { "UnknownTypeIsAString", UnknownTypeIsAString },
    { "NameNeverUsesAliases", NameNeverUsesAliases },
    { "FormatsTypesWithAliasOrHash", FormatsTypesWithAliasOrHash },
    { "FormatsNamesWithHashOrString", FormatsNamesWithHashOrString },
    { "FormatRoundTrips", FormatRoundTrips },
    { "ParsesLang", ParsesLang },
    { "Win32PointerConversion", Win32PointerConversion },
    { "KeyEquality", KeyEquality },
    { "FormatterWorks", FormatterWorks },
};

}  // namespace

int RunResourceIdTests()
{
    return RunGroup( "resource_id", kCases );
}

}  // namespace rcedit::test
