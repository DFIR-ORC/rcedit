//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <filesystem>

namespace rcedit::test {

// Path of rcedit_fixture.exe, given as argv[2] to rcedit_tests.
inline std::filesystem::path g_fixturePath;

}  // namespace rcedit::test
