//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include "core/version.h"

namespace rcedit::test {

namespace {

void VersionIsNotEmpty()
{
    CHECK( !Version().empty() );
    CHECK( Version().find( '.' ) != std::string_view::npos );
}

constexpr TestCase kCases[] = {
    { "VersionIsNotEmpty", VersionIsNotEmpty },
};

}  // namespace

int RunVersionTests()
{
    return RunGroup( "version", kCases );
}

}  // namespace rcedit::test
