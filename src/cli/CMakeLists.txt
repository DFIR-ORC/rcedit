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

# Anti side-loading link policy. /NODEFAULTLIB drops the libs the CRT would
# otherwise pull in via #pragma comment(lib, ...); CMake's implicit
# CMAKE_CXX_STANDARD_LIBRARIES (gdi32.lib, shell32.lib, ole32.lib,
# user32.lib, etc.) still lands on the link line regardless, so the explicit
# lib list below is not itself the guarantee that those libs contribute no
# imports -- it's just what rcedit actually needs. The real guarantee is the
# "imports" CTest (check_imports.cmake), which inspects the built exe's real
# PE import table.
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
