//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include "cli/args.h"
#include "cli/commands.h"
#include "core/codec.h"

namespace rcedit::test {

namespace {

using namespace rcedit::cli;

std::expected< ParsedArgs, UsageError > ParseOf(
    std::initializer_list< const wchar_t* > tokens )
{
    std::vector< std::wstring > argv;
    for( const wchar_t* t : tokens ) {
        argv.emplace_back( t );
    }

    return Parse( argv, Commands() );
}

void FiveCommandsInOrder()
{
    const auto commands = Commands();
    CHECK( commands.size() == 5 );
    CHECK( commands[ 0 ].name == L"list" );
    CHECK( commands[ 1 ].name == L"get" );
    CHECK( commands[ 2 ].name == L"set" );
    CHECK( commands[ 3 ].name == L"remove" );
    CHECK( commands[ 4 ].name == L"hexdump" );
    for( const auto& c : commands ) {
        CHECK( c.handle != nullptr );
        CHECK( !c.summary.empty() );
    }
}

void ListAcceptsFilters()
{
    CHECK( ParseOf( { L"list", L"a.exe" } ).has_value() );
    CHECK(
        ParseOf( { L"list", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG" } )
            .has_value() );
    auto bad = ParseOf( { L"list", L"a.exe", L"-t", L"#0" } );
    CHECK(
        !bad.has_value()
        && bad.error().message.find( L"--type" ) != std::wstring::npos );
}

void GetRequiresTypeNameOutput()
{
    CHECK( ParseOf(
               { L"get",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"CONFIG",
                 L"-o",
                 L"out.bin" } )
               .has_value() );
    CHECK(
        !ParseOf( { L"get", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG" } )
             .has_value() );
    CHECK( ParseOf(
               { L"get",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"CONFIG",
                 L"-o",
                 L"o",
                 L"-l",
                 L"0x409",
                 L"--raw" } )
               .has_value() );
    auto bad = ParseOf(
        { L"get",
          L"a.exe",
          L"-t",
          L"RT_RCDATA",
          L"-n",
          L"CONFIG",
          L"-o",
          L"o",
          L"-l",
          L"abc" } );
    CHECK(
        !bad.has_value()
        && bad.error().message.find( L"--lang" ) != std::wstring::npos );
}

void SetRequiresExactlyOneValueSource()
{
    CHECK( ParseOf(
               { L"set",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"CONFIG",
                 L"--value",
                 L"x" } )
               .has_value() );
    CHECK( ParseOf(
               { L"set",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"CONFIG",
                 L"--value-utf16",
                 L"x" } )
               .has_value() );
    CHECK( ParseOf(
               { L"set",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"CONFIG",
                 L"--value-path",
                 L"f" } )
               .has_value() );

    auto none =
        ParseOf( { L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG" } );
    CHECK(
        !none.has_value()
        && none.error().message.find( L"--value" ) != std::wstring::npos );
    auto two = ParseOf(
        { L"set",
          L"a.exe",
          L"-t",
          L"RT_RCDATA",
          L"-n",
          L"CONFIG",
          L"--value",
          L"x",
          L"--value-path",
          L"f" } );
    CHECK( !two.has_value() );
}

void SetCompressValidation()
{
    CHECK( ParseOf(
               { L"set",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"C",
                 L"--value",
                 L"x",
                 L"-c",
                 L"none" } )
               .has_value() );

    auto unknown = ParseOf(
        { L"set",
          L"a.exe",
          L"-t",
          L"RT_RCDATA",
          L"-n",
          L"C",
          L"--value",
          L"x",
          L"-c",
          L"lz4" } );
    CHECK(
        !unknown.has_value()
        && unknown.error().message.find( L"lz4" ) != std::wstring::npos );

    auto zstd = ParseOf(
        { L"set",
          L"a.exe",
          L"-t",
          L"RT_RCDATA",
          L"-n",
          L"C",
          L"--value",
          L"x",
          L"-c",
          L"zstd" } );
    auto sevenZip = ParseOf(
        { L"set",
          L"a.exe",
          L"-t",
          L"RT_RCDATA",
          L"-n",
          L"C",
          L"--value",
          L"x",
          L"-c",
          L"7z" } );
#ifdef RCEDIT_HAS_ZSTD
    CHECK( zstd.has_value() );
#else
    CHECK(
        !zstd.has_value()
        && zstd.error().message.find( L"disabled" ) != std::wstring::npos );
#endif
#ifdef RCEDIT_HAS_7Z
    CHECK( sevenZip.has_value() );
#else
    CHECK(
        !sevenZip.has_value()
        && sevenZip.error().message.find( L"disabled" ) != std::wstring::npos );
#endif
}

void RemoveAndHexdump()
{
    CHECK( ParseOf( { L"remove", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C" } )
               .has_value() );
    CHECK( ParseOf(
               { L"remove",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"C",
                 L"-o",
                 L"b.exe" } )
               .has_value() );
    CHECK(
        !ParseOf( { L"remove", L"a.exe", L"-t", L"RT_RCDATA" } ).has_value() );

    CHECK( ParseOf(
               { L"hexdump",
                 L"a.exe",
                 L"-t",
                 L"RT_RCDATA",
                 L"-n",
                 L"C",
                 L"--limit",
                 L"32" } )
               .has_value() );
    auto bad = ParseOf(
        { L"hexdump",
          L"a.exe",
          L"-t",
          L"RT_RCDATA",
          L"-n",
          L"C",
          L"--limit",
          L"x" } );
    CHECK(
        !bad.has_value()
        && bad.error().message.find( L"--limit" ) != std::wstring::npos );
}

void CompressHelpListsCompiledCodecs()
{
    const std::wstring usage = FormatCommandUsage( Commands()[ 2 ] );
    CHECK( usage.find( L"none" ) != std::wstring::npos );
    const bool mentionsZstd = usage.find( L"zstd" ) != std::wstring::npos;
    const bool mentions7z = usage.find( L"7z" ) != std::wstring::npos;
#ifdef RCEDIT_HAS_ZSTD
    CHECK( mentionsZstd );
#else
    CHECK( !mentionsZstd );
#endif
#ifdef RCEDIT_HAS_7Z
    CHECK( mentions7z );
#else
    CHECK( !mentions7z );
#endif
}

constexpr TestCase kCases[] = {
    { "FiveCommandsInOrder", FiveCommandsInOrder },
    { "ListAcceptsFilters", ListAcceptsFilters },
    { "GetRequiresTypeNameOutput", GetRequiresTypeNameOutput },
    { "SetRequiresExactlyOneValueSource", SetRequiresExactlyOneValueSource },
    { "SetCompressValidation", SetCompressValidation },
    { "RemoveAndHexdump", RemoveAndHexdump },
    { "CompressHelpListsCompiledCodecs", CompressHelpListsCompiledCodecs },
};

}  // namespace

int RunCommandsTests()
{
    return RunGroup( "commands", kCases );
}

}  // namespace rcedit::test
