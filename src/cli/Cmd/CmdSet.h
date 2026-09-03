//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "cli/args.h"

namespace rcedit::cli {

// Returns the spec that registers the 'set' command.
[[nodiscard]] CommandSpec GetSetCommandSpec();

}  // namespace rcedit::cli
