//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include "cli/args.h"

namespace rcedit::test {

namespace {

using namespace rcedit::cli;

constexpr OptionSpec kAlphaOptions[] = {
    { L"type", L't', true, true, L"TYPE", L"Resource type" },
    { L"name", L'n', true, false, L"NAME", L"Resource name" },
    { L"raw", 0, false, false, L"", L"Raw output" },
};

constexpr OptionSpec kBetaOptions[] = {
    { L"value", 0, true, false, L"TEXT", L"Inline value" },
    { L"value-path", 0, true, false, L"FILE", L"Value file" },
};

std::optional< std::wstring > ValidateBeta( const ParsedArgs& args )
{
    const int count = ( args.Has( L"value" ) ? 1 : 0 )
        + ( args.Has( L"value-path" ) ? 1 : 0 );
    if( count != 1 ) {
        return L"exactly one of --value or --value-path is required";
    }

    return std::nullopt;
}

int RunNoop( const ParsedArgs& )
{
    return 0;
}

const CommandSpec kCommands[] = {
    { L"alpha", L"Alpha command", kAlphaOptions, nullptr, RunNoop },
    { L"beta", L"Beta command", kBetaOptions, ValidateBeta, RunNoop },
};

std::expected< ParsedArgs, UsageError > ParseOf(
    std::initializer_list< const wchar_t* > tokens )
{
    std::vector< std::wstring > argv;
    for( const wchar_t* t : tokens ) {
        argv.emplace_back( t );
    }

    return Parse( argv, kCommands );
}

void ParsesCommandPathAndOptions()
{
    auto r = ParseOf(
        { L"alpha",
          L"a.exe",
          L"--type",
          L"RT_RCDATA",
          L"-n",
          L"CONFIG",
          L"--raw" } );
    CHECK( r.has_value() );
    CHECK( r->command == &kCommands[ 0 ] );
    CHECK( r->pePath == L"a.exe" );
    CHECK( r->Value( L"type" ) == L"RT_RCDATA" );
    CHECK( r->Value( L"name" ) == L"CONFIG" );
    CHECK( r->Has( L"raw" ) );
    CHECK( !r->Has( L"missing" ) );
    CHECK( !r->Value( L"missing" ).has_value() );
}

void OptionsMayPrecedeThePath()
{
    auto r = ParseOf( { L"alpha", L"-t", L"X", L"a.exe" } );
    CHECK(
        r.has_value() && r->pePath == L"a.exe" && r->Value( L"type" ) == L"X" );
}

void GlobalsAnywhere()
{
    auto a = ParseOf( { L"--verbose", L"alpha", L"a.exe", L"-t", L"X" } );
    CHECK( a.has_value() && a->verbose && !a->quiet );
    auto b = ParseOf( { L"alpha", L"a.exe", L"-t", L"X", L"-q" } );
    CHECK( b.has_value() && b->quiet );
    auto c = ParseOf( { L"alpha", L"a.exe", L"-t", L"X", L"-v", L"-q" } );
    CHECK( !c.has_value() );
}

void HelpAndVersionShortCircuit()
{
    auto a = ParseOf( { L"--help" } );
    CHECK( a.has_value() && a->help && a->command == nullptr );
    auto b = ParseOf( { L"alpha", L"-h" } );
    CHECK( b.has_value() && b->help && b->command == &kCommands[ 0 ] );
    auto c = ParseOf( { L"--version" } );
    CHECK( c.has_value() && c->version );
    auto d = ParseOf(
        { L"beta", L"--help" } );  // required checks and validate skipped
    CHECK( d.has_value() && d->help );
}

void MissingCommandOrPath()
{
    auto a = ParseOf( {} );
    CHECK( !a.has_value() && a.error().command == nullptr );
    CHECK( a.error().message.find( L"command" ) != std::wstring::npos );

    auto b = ParseOf( { L"alpha", L"-t", L"X" } );
    CHECK( !b.has_value() && b.error().command == &kCommands[ 0 ] );
    CHECK( b.error().message.find( L"pe_file" ) != std::wstring::npos );

    auto c = ParseOf( { L"gamma", L"a.exe" } );
    CHECK(
        !c.has_value()
        && c.error().message.find( L"gamma" ) != std::wstring::npos );
}

void ExtraPositionalIsAnError()
{
    auto r = ParseOf( { L"alpha", L"a.exe", L"b.exe", L"-t", L"X" } );
    CHECK(
        !r.has_value()
        && r.error().message.find( L"b.exe" ) != std::wstring::npos );
}

void UnknownDuplicateAndMissingValue()
{
    auto a = ParseOf( { L"alpha", L"a.exe", L"-t", L"X", L"--bogus" } );
    CHECK(
        !a.has_value()
        && a.error().message.find( L"--bogus" ) != std::wstring::npos );

    auto b = ParseOf( { L"alpha", L"a.exe", L"-t", L"X", L"-t", L"Y" } );
    CHECK(
        !b.has_value()
        && b.error().message.find( L"--type" ) != std::wstring::npos );

    auto c = ParseOf( { L"alpha", L"a.exe", L"-t" } );
    CHECK(
        !c.has_value()
        && c.error().message.find( L"value" ) != std::wstring::npos );

    auto d = ParseOf( { L"alpha", L"a.exe", L"-t", L"X", L"-x" } );
    CHECK( !d.has_value() );

    auto e = ParseOf( { L"--bogus", L"alpha", L"a.exe" } );
    CHECK( !e.has_value() && e.error().command == nullptr );
}

void RequiredOptionEnforced()
{
    auto r = ParseOf( { L"alpha", L"a.exe", L"-n", L"CONFIG" } );
    CHECK(
        !r.has_value()
        && r.error().message.find( L"--type" ) != std::wstring::npos );
}

void ValuesMayStartWithDash()
{
    auto r = ParseOf( { L"alpha", L"a.exe", L"-t", L"-weird" } );
    CHECK( r.has_value() && r->Value( L"type" ) == L"-weird" );
}

void DoubleDashEndsOptions()
{
    auto r = ParseOf( { L"alpha", L"-t", L"X", L"--", L"-file.exe" } );
    CHECK( r.has_value() && r->pePath == L"-file.exe" );
}

void ValidateIsCalled()
{
    auto a = ParseOf( { L"beta", L"a.exe" } );
    CHECK(
        !a.has_value()
        && a.error().message.find( L"exactly one" ) != std::wstring::npos );
    auto b = ParseOf(
        { L"beta", L"a.exe", L"--value", L"x", L"--value-path", L"f" } );
    CHECK( !b.has_value() );
    auto c = ParseOf( { L"beta", L"a.exe", L"--value-path", L"f" } );
    CHECK( c.has_value() );
}

void UsageMentionsEverything()
{
    const std::wstring usage = FormatUsage( kCommands );
    CHECK( usage.find( L"alpha" ) != std::wstring::npos );
    CHECK( usage.find( L"Beta command" ) != std::wstring::npos );
    CHECK( usage.find( L"--verbose" ) != std::wstring::npos );
    CHECK( usage.find( L"--version" ) != std::wstring::npos );

    const std::wstring alpha = FormatCommandUsage( kCommands[ 0 ] );
    CHECK( alpha.find( L"-t, --type <TYPE>" ) != std::wstring::npos );
    CHECK( alpha.find( L"Resource type" ) != std::wstring::npos );
    CHECK( alpha.find( L"(required)" ) != std::wstring::npos );
    CHECK( alpha.find( L"--raw" ) != std::wstring::npos );
    CHECK( alpha.find( L"<pe_file>" ) != std::wstring::npos );
}

constexpr TestCase kCases[] = {
    { "ParsesCommandPathAndOptions", ParsesCommandPathAndOptions },
    { "OptionsMayPrecedeThePath", OptionsMayPrecedeThePath },
    { "GlobalsAnywhere", GlobalsAnywhere },
    { "HelpAndVersionShortCircuit", HelpAndVersionShortCircuit },
    { "MissingCommandOrPath", MissingCommandOrPath },
    { "ExtraPositionalIsAnError", ExtraPositionalIsAnError },
    { "UnknownDuplicateAndMissingValue", UnknownDuplicateAndMissingValue },
    { "RequiredOptionEnforced", RequiredOptionEnforced },
    { "ValuesMayStartWithDash", ValuesMayStartWithDash },
    { "DoubleDashEndsOptions", DoubleDashEndsOptions },
    { "ValidateIsCalled", ValidateIsCalled },
    { "UsageMentionsEverything", UsageMentionsEverything },
};

}  // namespace

int RunArgsTests()
{
    return RunGroup( "args", kCases );
}

}  // namespace rcedit::test
