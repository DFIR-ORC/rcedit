//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace rcedit::Out {

// Program output: UTF-8 to stdout, no decoration.
void Write( std::wstring_view text );

template < typename... Args >
void Print( std::wformat_string< Args... > fmt, Args&&... args )
{
    Write(
        std::wstring_view(
            std::format( fmt, std::forward< Args >( args )... ) ) );
}

}  // namespace rcedit::Out
