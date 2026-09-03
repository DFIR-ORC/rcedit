#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
# Usage: cmake -DDUMPBIN=<path> -DEXE=<path> -DALLOWED="KERNEL32.dll;OLEAUT32.dll" -P check_imports.cmake
# Fails when the executable imports any DLL outside ALLOWED (case-insensitive).

if(NOT DUMPBIN OR NOT EXE OR NOT DEFINED ALLOWED)
    message(FATAL_ERROR "check_imports.cmake needs DUMPBIN, EXE and ALLOWED")
endif()

execute_process(
    COMMAND "${DUMPBIN}" /nologo /imports "${EXE}"
    OUTPUT_VARIABLE output
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "dumpbin failed (${result}) on ${EXE}")
endif()

set(allowed_upper "")
foreach(dll IN LISTS ALLOWED)
    string(TOUPPER "${dll}" u)
    list(APPEND allowed_upper "${u}")
endforeach()

# dumpbin prints each imported module on its own line, indented, ending in .dll
string(REGEX MATCHALL "\n +([^ \t\r\n]+\\.[dD][lL][lL])" lines "${output}")
set(found "")
foreach(line IN LISTS lines)
    string(REGEX REPLACE "^\n +" "" dll "${line}")
    list(APPEND found "${dll}")
endforeach()
list(REMOVE_DUPLICATES found)

set(forbidden "")
foreach(dll IN LISTS found)
    string(TOUPPER "${dll}" u)
    if(NOT u IN_LIST allowed_upper)
        list(APPEND forbidden "${dll}")
    endif()
endforeach()

if(forbidden)
    message(FATAL_ERROR "Forbidden imports in ${EXE}: ${forbidden} (allowed: ${ALLOWED})")
endif()
message(STATUS "Imports of ${EXE}: ${found} (all allowed)")
