//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <span>

#include "cli/args.h"

namespace rcedit::cli {

// list, get, set, remove, hexdump.
[[nodiscard]] std::span< const CommandSpec > Commands();

}  // namespace rcedit::cli
