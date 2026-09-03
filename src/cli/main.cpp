//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include <print>

#include "core/version.h"

int wmain( int, const wchar_t* const[] )
{
    std::print( "rcedit {}\n", rcedit::Version() );
    return 0;
}
