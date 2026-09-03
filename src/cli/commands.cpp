//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/commands.h"

#include <array>

#include "cli/Cmd/CmdGet.h"
#include "cli/Cmd/CmdHexdump.h"
#include "cli/Cmd/CmdList.h"
#include "cli/Cmd/CmdRemove.h"
#include "cli/Cmd/CmdSet.h"

namespace rcedit::cli {

namespace {

// Declaration order is the order shown in usage output.
const std::array< CommandSpec, 5 > kCommands = {
    GetListCommandSpec(),   GetGetCommandSpec(),     GetSetCommandSpec(),
    GetRemoveCommandSpec(), GetHexdumpCommandSpec(),
};

}  // namespace

std::span< const CommandSpec > Commands()
{
    return kCommands;
}

}  // namespace rcedit::cli
