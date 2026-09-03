//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/version.h"

namespace rcedit {

std::string_view Version() noexcept
{
    return RCEDIT_VERSION;
}

}  // namespace rcedit
