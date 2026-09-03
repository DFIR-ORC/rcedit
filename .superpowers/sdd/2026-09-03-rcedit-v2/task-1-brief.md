### Task 1: Scaffold, build system, test harness, import check

**Files:**
- Delete: `src/` (whole old tree), `CMakeLists.txt`
- Create: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `.gitignore`, `src/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/core/version.h`, `src/core/version.cpp`, `src/cli/CMakeLists.txt`, `src/cli/main.cpp`, `tests/CMakeLists.txt`, `tests/check.h`, `tests/main.cpp`, `tests/context.h`, `tests/test_version.cpp`, `tests/check_imports.cmake`
- Create (copied): `external/vcpkg_overlay_ports/7zip/*`, `external/vcpkg_overlay_triplets/x64-windows-static.cmake`
- Submodule: `external/vcpkg`

**Interfaces:**
- Produces: `std::string_view rcedit::Version()`; test harness `CHECK(expr)`, `struct TestCase { const char* name; void (*fn)(); }`, `int RunGroup(std::string_view, std::span<const TestCase>)`; `tests/main.cpp` dispatch table `kGroups` that later tasks append to; CMake variable `RCEDIT_CORE_SOURCES` pattern in `src/core/CMakeLists.txt`; CMake function `rcedit_add_import_check(target)`.

- [ ] **Step 1: Remove the old tree**

```powershell
git rm -r -q src CMakeLists.txt
```

Keep `.clang-format` and `docs/`.

- [ ] **Step 2: Add the vcpkg submodule and copy overlays**

```powershell
git submodule add https://github.com/microsoft/vcpkg external/vcpkg
git -C external/vcpkg checkout cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3
New-Item -ItemType Directory -Force external/vcpkg_overlay_ports, external/vcpkg_overlay_triplets | Out-Null
Copy-Item -Recurse S:\llm\dfir-orc-forge\external\vcpkg_overlay_ports\7zip external\vcpkg_overlay_ports\7zip
Copy-Item S:\llm\dfir-orc-forge\external\vcpkg_overlay_triplets\x64-windows-static.cmake external\vcpkg_overlay_triplets\
```

Expected: `external/vcpkg_overlay_ports/7zip` contains `portfile.cmake`, `CMakeLists.txt`, `7zip.h`, `extras.h`, `guids.h`, `Archive2.def`, `7zip-config.cmake.in`, `vcpkg.json`, and the three `.patch` files.

- [ ] **Step 3: Write `vcpkg.json`**

```json
{
  "name": "rcedit",
  "version": "2.0.0",
  "description": "Edit resources of Windows PE files",
  "license": "LGPL-2.1-or-later",
  "dependencies": [],
  "features": {
    "7z": {
      "description": "7z compression of resource payloads",
      "dependencies": ["7zip"]
    },
    "zstd": {
      "description": "zstd compression of resource payloads",
      "dependencies": ["zstd"]
    }
  }
}
```

- [ ] **Step 4: Write the top-level `CMakeLists.txt`**

```cmake
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

option(RCEDIT_ENABLE_7Z "Build the 7z codec (links 7-Zip statically)" ON)
option(RCEDIT_ENABLE_ZSTD "Build the zstd codec (links zstd statically)" ON)
option(RCEDIT_BUILD_TESTS "Build and register tests" ON)

# vcpkg must be wired before project() so the toolchain sees it.
set(RCEDIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT DEFINED VCPKG_ROOT)
    set(VCPKG_ROOT "${RCEDIT_ROOT}/external/vcpkg")
endif()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
endif()
set(VCPKG_OVERLAY_PORTS "${RCEDIT_ROOT}/external/vcpkg_overlay_ports")
set(VCPKG_OVERLAY_TRIPLETS "${RCEDIT_ROOT}/external/vcpkg_overlay_triplets")
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

set(VCPKG_MANIFEST_FEATURES "")
if(RCEDIT_ENABLE_7Z)
    list(APPEND VCPKG_MANIFEST_FEATURES "7z")
endif()
if(RCEDIT_ENABLE_ZSTD)
    list(APPEND VCPKG_MANIFEST_FEATURES "zstd")
endif()

project(rcedit VERSION 2.0.0 LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "rcedit requires MSVC")
endif()
if(BUILD_SHARED_LIBS)
    message(FATAL_ERROR "rcedit is static only: BUILD_SHARED_LIBS must be OFF")
endif()
if(NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")
    message(FATAL_ERROR "rcedit is static only: triplet must be x64-windows-static (got ${VCPKG_TARGET_TRIPLET})")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(/W4 /WX /guard:cf /EHsc /sdl /utf-8 /external:W0)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
add_link_options(/guard:cf)

add_subdirectory(src)

if(RCEDIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 5: Write `src/CMakeLists.txt`, `src/core/CMakeLists.txt`, `version.h/.cpp`**

`src/CMakeLists.txt`:
```cmake
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
add_subdirectory(core)
add_subdirectory(cli)
```

`src/core/CMakeLists.txt`:
```cmake
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
set(RCEDIT_CORE_SOURCES
    version.h
    version.cpp
)

add_library(rcedit_core STATIC ${RCEDIT_CORE_SOURCES})

target_include_directories(rcedit_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_definitions(rcedit_core
    PRIVATE RCEDIT_VERSION="${PROJECT_VERSION}"
)

if(RCEDIT_ENABLE_7Z)
    target_compile_definitions(rcedit_core PUBLIC RCEDIT_HAS_7Z)
endif()
if(RCEDIT_ENABLE_ZSTD)
    target_compile_definitions(rcedit_core PUBLIC RCEDIT_HAS_ZSTD)
endif()
```

`src/core/version.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <string_view>

namespace rcedit {

[[nodiscard]] std::string_view Version() noexcept;

}  // namespace rcedit
```

`src/core/version.cpp`:
```cpp
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
```

- [ ] **Step 6: Write `src/cli/CMakeLists.txt` with the link policy and a stub `main.cpp`**

`src/cli/CMakeLists.txt`:
```cmake
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
add_executable(rcedit
    main.cpp
)

target_link_libraries(rcedit PRIVATE rcedit_core)

# Anti side-loading link policy: no default libraries, explicit allowlist.
target_link_options(rcedit PRIVATE /NODEFAULTLIB)

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
```

`src/cli/main.cpp` (stub, replaced in Task 11):
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include <print>

#include "core/version.h"

int wmain(int, const wchar_t* const[])
{
    std::print("rcedit {}\n", rcedit::Version());
    return 0;
}
```

- [ ] **Step 7: Write `CMakePresets.json`**

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja Multi-Config",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "toolchainFile": "${sourceDir}/external/vcpkg/scripts/buildsystems/vcpkg.cmake",
      "cacheVariables": {
        "VCPKG_ROOT": "${sourceDir}/external/vcpkg",
        "VCPKG_TARGET_TRIPLET": "x64-windows-static"
      },
      "environment": { "VCPKG_FEATURE_FLAGS": "manifests,versions" }
    },
    { "name": "default", "inherits": "base",
      "cacheVariables": { "RCEDIT_ENABLE_7Z": "ON", "RCEDIT_ENABLE_ZSTD": "ON" } },
    { "name": "no-7z", "inherits": "base",
      "cacheVariables": { "RCEDIT_ENABLE_7Z": "OFF", "RCEDIT_ENABLE_ZSTD": "ON" } },
    { "name": "no-zstd", "inherits": "base",
      "cacheVariables": { "RCEDIT_ENABLE_7Z": "ON", "RCEDIT_ENABLE_ZSTD": "OFF" } },
    { "name": "minimal", "inherits": "base",
      "cacheVariables": { "RCEDIT_ENABLE_7Z": "OFF", "RCEDIT_ENABLE_ZSTD": "OFF" } }
  ],
  "buildPresets": [
    { "name": "default-Debug", "configurePreset": "default", "configuration": "Debug" },
    { "name": "default-RelWithDebInfo", "configurePreset": "default", "configuration": "RelWithDebInfo" },
    { "name": "default-MinSizeRel", "configurePreset": "default", "configuration": "MinSizeRel" },
    { "name": "no-7z-MinSizeRel", "configurePreset": "no-7z", "configuration": "MinSizeRel" },
    { "name": "no-zstd-MinSizeRel", "configurePreset": "no-zstd", "configuration": "MinSizeRel" },
    { "name": "minimal-MinSizeRel", "configurePreset": "minimal", "configuration": "MinSizeRel" }
  ],
  "testPresets": [
    { "name": "default-Debug", "configurePreset": "default", "configuration": "Debug", "output": { "outputOnFailure": true } },
    { "name": "default-RelWithDebInfo", "configurePreset": "default", "configuration": "RelWithDebInfo", "output": { "outputOnFailure": true } },
    { "name": "default-MinSizeRel", "configurePreset": "default", "configuration": "MinSizeRel", "output": { "outputOnFailure": true } },
    { "name": "no-7z-MinSizeRel", "configurePreset": "no-7z", "configuration": "MinSizeRel", "output": { "outputOnFailure": true } },
    { "name": "no-zstd-MinSizeRel", "configurePreset": "no-zstd", "configuration": "MinSizeRel", "output": { "outputOnFailure": true } },
    { "name": "minimal-MinSizeRel", "configurePreset": "minimal", "configuration": "MinSizeRel", "output": { "outputOnFailure": true } }
  ]
}
```

`.gitignore`:
```
build/
.vs/
*.user
```

Run every `cmake` command from a Visual Studio 2022 x64 developer shell (Ninja and `cl` on `PATH`). In PowerShell: `& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64` (adjust edition).

- [ ] **Step 8: Write the test harness**

`tests/check.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <print>
#include <span>
#include <string_view>

namespace rcedit::test {

struct TestCase
{
    const char* name;
    void (*fn)();
};

inline int g_failures = 0;

inline void Fail(const char* expr, const char* file, int line)
{
    ++g_failures;
    std::print(stderr, "  FAIL {}:{}: {}\n", file, line, expr);
}

inline int RunGroup(std::string_view group, std::span<const TestCase> cases)
{
    for (const auto& tc : cases)
    {
        const int before = g_failures;
        std::print("[{}] {}\n", group, tc.name);
        tc.fn();
        if (g_failures != before)
        {
            std::print("  -> FAILED\n");
        }
    }
    std::print("{}: {} case(s), {} failure(s)\n", group, cases.size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace rcedit::test

#define CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            ::rcedit::test::Fail(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

#define CHECK_EC_OK(ec) \
    do \
    { \
        if ((ec)) \
        { \
            ::rcedit::test::Fail(#ec " is an error", __FILE__, __LINE__); \
        } \
    } while (0)
```

`tests/context.h`:
```cpp
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
```

`tests/test_version.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include "core/version.h"

namespace rcedit::test {

namespace {

void VersionIsNotEmpty()
{
    CHECK(!Version().empty());
    CHECK(Version().find('.') != std::string_view::npos);
}

constexpr TestCase kCases[] = {
    {"VersionIsNotEmpty", VersionIsNotEmpty},
};

}  // namespace

int RunVersionTests()
{
    return RunGroup("version", kCases);
}

}  // namespace rcedit::test
```

`tests/main.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include <print>
#include <string_view>

#include "context.h"

namespace rcedit::test {

int RunVersionTests();
// Later tasks add: RunErrorTests, RunEncodingTests, RunResourceIdTests,
// RunCodecTests, RunEngineTests, RunOpsTests, RunArgsTests.

struct Group
{
    std::string_view name;
    int (*run)();
};

constexpr Group kGroups[] = {
    {"version", RunVersionTests},
};

}  // namespace rcedit::test

int wmain(int argc, wchar_t* argv[])
{
    using namespace rcedit::test;

    if (argc < 2)
    {
        std::print(stderr, "usage: rcedit_tests <group> [fixture_path]\n");
        return 2;
    }

    if (argc >= 3)
    {
        g_fixturePath = argv[2];
    }

    std::wstring_view wanted(argv[1]);
    for (const auto& g : kGroups)
    {
        std::wstring name(g.name.begin(), g.name.end());
        if (name == wanted)
        {
            return g.run();
        }
    }

    std::print(stderr, "unknown group\n");
    return 2;
}
```

- [ ] **Step 9: Write the import check script and `tests/CMakeLists.txt`**

`tests/check_imports.cmake`:
```cmake
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
```

`tests/CMakeLists.txt`:
```cmake
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

# Import allowlist check on the real executable.
get_filename_component(_vc_bin "${CMAKE_LINKER}" DIRECTORY)
find_program(RCEDIT_DUMPBIN dumpbin HINTS "${_vc_bin}" REQUIRED)

set(RCEDIT_ALLOWED_IMPORTS "KERNEL32.dll")
if(RCEDIT_ENABLE_7Z)
    list(APPEND RCEDIT_ALLOWED_IMPORTS "OLEAUT32.dll")
endif()

add_test(
    NAME imports
    COMMAND "${CMAKE_COMMAND}"
        "-DDUMPBIN=${RCEDIT_DUMPBIN}"
        "-DEXE=$<TARGET_FILE:rcedit>"
        "-DALLOWED=${RCEDIT_ALLOWED_IMPORTS}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_imports.cmake"
)
```

- [ ] **Step 10: Configure and build the `minimal` preset, run tests**

```powershell
cmake --preset minimal
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
.\build\minimal\MinSizeRel\rcedit.exe
```

Expected: configure succeeds with vcpkg installing nothing; both tests `version` and `imports` pass; the exe prints `rcedit 2.0.0`. If `imports` lists a DLL other than `KERNEL32.dll`, stop and report it.

- [ ] **Step 11: Configure and build the `default` preset**

```powershell
cmake --preset default
cmake --build --preset default-MinSizeRel
ctest --preset default-MinSizeRel
```

Expected: vcpkg builds `7zip` and `zstd` (several minutes the first time); tests pass. Nothing links the codecs yet, so `imports` still shows only kernel32.

- [ ] **Step 12: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h src/cli/*.cpp tests/*.cpp tests/*.h
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Scaffold rcedit v2: CMake, vcpkg, presets, test harness, import check"
```

---

