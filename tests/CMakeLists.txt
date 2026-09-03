#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
add_executable(rcedit_tests
    check.h
    context.h
    main.cpp
    test_version.cpp
)
target_link_libraries(rcedit_tests PRIVATE rcedit_core)
target_include_directories(rcedit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# One CTest per group. Later tasks append to RCEDIT_TEST_GROUPS.
set(RCEDIT_TEST_GROUPS version)
foreach(group IN LISTS RCEDIT_TEST_GROUPS)
    add_test(NAME ${group} COMMAND rcedit_tests ${group})
endforeach()

# Import allowlist check on a target's real executable.
get_filename_component(_vc_bin "${CMAKE_LINKER}" DIRECTORY)
find_program(RCEDIT_DUMPBIN dumpbin HINTS "${_vc_bin}" REQUIRED)

function(rcedit_add_import_check target)
    set(allowed "KERNEL32.dll")
    if(RCEDIT_ENABLE_7Z)
        list(APPEND allowed "OLEAUT32.dll")
    endif()

    add_test(
        NAME imports
        COMMAND "${CMAKE_COMMAND}"
            "-DDUMPBIN=${RCEDIT_DUMPBIN}"
            "-DEXE=$<TARGET_FILE:${target}>"
            "-DALLOWED=${allowed}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_imports.cmake"
    )
endfunction()

rcedit_add_import_check(rcedit)
