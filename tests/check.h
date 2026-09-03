//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <print>
#include <span>
#include <string_view>

namespace rcedit::test {

struct TestCase
{
    const char* name;
    void ( *fn )();
};

inline int g_failures = 0;

inline void Fail( const char* expr, const char* file, int line )
{
    ++g_failures;
    std::print( stderr, "  FAIL {}:{}: {}\n", file, line, expr );
}

inline int RunGroup( std::string_view group, std::span< const TestCase > cases )
{
    for( const auto& tc : cases ) {
        const int before = g_failures;
        std::print( "[{}] {}\n", group, tc.name );
        tc.fn();
        if( g_failures != before ) {
            std::print( "  -> FAILED\n" );
        }
    }
    std::print(
        "{}: {} case(s), {} failure(s)\n", group, cases.size(), g_failures );
    return g_failures == 0 ? 0 : 1;
}

}  // namespace rcedit::test

#define CHECK( expr )                                                          \
    do {                                                                       \
        if( !( expr ) ) {                                                      \
            ::rcedit::test::Fail( #expr, __FILE__, __LINE__ );                 \
        }                                                                      \
    } while( 0 )

#define CHECK_EC_OK( ec )                                                      \
    do {                                                                       \
        if( ( ec ) ) {                                                         \
            ::rcedit::test::Fail( #ec " is an error", __FILE__, __LINE__ );    \
        }                                                                      \
    } while( 0 )
