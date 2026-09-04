//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <system_error>

#include "cli/args.h"

namespace rcedit::cli {

[[nodiscard]] std::error_code HandleGet( const ParsedArgs& args );

// Returns the spec that registers the 'get' command.
[[nodiscard]] CommandSpec GetGetCommandSpec();

}  // namespace rcedit::cli
