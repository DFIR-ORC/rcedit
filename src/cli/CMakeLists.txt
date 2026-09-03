#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
add_executable(rcedit
    main.cpp
    args.h
    args.cpp
    commands.h
    commands.cpp
)

target_link_libraries(rcedit PRIVATE rcedit_core)

# Anti side-loading link policy: no default libraries, explicit allowlist.
target_link_options(rcedit PRIVATE /NODEFAULTLIB /DEBUG /OPT:REF /OPT:ICF)

set(RCEDIT_CRT_LIBS
    $<IF:$<CONFIG:Debug>,libcmtd.lib,libcmt.lib>
    $<IF:$<CONFIG:Debug>,libucrtd.lib,libucrt.lib>
    $<IF:$<CONFIG:Debug>,libvcruntimed.lib,libvcruntime.lib>
    $<IF:$<CONFIG:Debug>,libcpmtd.lib,libcpmt.lib>
)

target_link_libraries(rcedit PRIVATE kernel32.lib ${RCEDIT_CRT_LIBS})

if(RCEDIT_ENABLE_7Z)
    # 7-Zip uses SysAllocString/VariantClear (OLEAUT32 is a KnownDLL) and IID_IUnknown (uuid.lib is static).
    target_link_libraries(rcedit PRIVATE oleaut32.lib uuid.lib)
endif()

set_target_properties(rcedit PROPERTIES OUTPUT_NAME rcedit)
