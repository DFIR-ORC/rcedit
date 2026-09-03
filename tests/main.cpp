//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include <print>
#include <string_view>

#include "context.h"

namespace rcedit::test {

int RunVersionTests();
int RunErrorTests();
int RunEncodingTests();
int RunResourceIdTests();
int RunCodecTests();
#ifdef RCEDIT_HAS_7Z
int RunSevenZipTests();
#endif
// Later tasks add: RunEngineTests, RunOpsTests, RunArgsTests.

struct Group
{
    std::string_view name;
    int ( *run )();
};

constexpr Group kGroups[] = {
    { "version", RunVersionTests },   { "error", RunErrorTests },
    { "encoding", RunEncodingTests }, { "resource_id", RunResourceIdTests },
    { "codec", RunCodecTests },
#ifdef RCEDIT_HAS_7Z
    { "sevenzip", RunSevenZipTests },
#endif
};

}  // namespace rcedit::test

int wmain( int argc, wchar_t* argv[] )
{
    using namespace rcedit::test;

    if( argc < 2 ) {
        std::print( stderr, "usage: rcedit_tests <group> [fixture_path]\n" );
        return 2;
    }

    if( argc >= 3 ) {
        g_fixturePath = argv[ 2 ];
    }

    std::wstring_view wanted( argv[ 1 ] );
    for( const auto& g : kGroups ) {
        std::wstring name( g.name.begin(), g.name.end() );
        if( name == wanted ) {
            return g.run();
        }
    }

    std::print( stderr, "unknown group\n" );
    return 2;
}
