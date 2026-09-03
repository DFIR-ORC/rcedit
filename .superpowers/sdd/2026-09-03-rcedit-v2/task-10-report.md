# Task 10 report: Commands, entry point, CLI smoke test

Status: **BLOCKED** (per R5's explicit stop condition — a real import outside
the allowlist showed up on the `imports` CTest for the `no-zstd` and
`default` presets). No commit was created; the working tree has the
implementation staged but uncommitted so it can be inspected or fixed.

## What was implemented (transcribed from the brief, plus R12)

- `src/cli/commands.h`, `src/cli/commands.cpp` — the `list`/`get`/`set`/
  `remove`/`hexdump` command table, `validate`/`run` handlers, transcribed
  verbatim from the brief with two changes:
  - **R12(a)**: `ParseLimit`'s digit-accumulation loop now has an overflow
    guard (`if (value > (max - digit) / 10) return unexpected(...)`) and
    `#include <limits>` was added.
  - **R12(b)**: every `Run*` handler (`RunList`, `RunGet`, `RunSet`,
    `RunRemove`, `RunHexdump`) now guards each `ParseKey`/`ParseLimit`/
    `ParseCompress`/`OutputPath`/`args.Value("value-path")` result with
    `if (!x) { return kUsage; }` before dereferencing it. Added
    `constexpr int kUsage = 2;` to the anonymous namespace (main.cpp's
    `kUsageError` is already 2, so `RunHexdump`/etc. exit 2 consistently
    with a re-parse failure, which validate should already have prevented).
- `src/cli/main.cpp` — replaced the stub with the real `wmain`/`Main`,
  transcribed verbatim from the brief.
- `src/cli/CMakeLists.txt` — added `commands.h`/`commands.cpp` to the
  `rcedit` executable's sources.
- `tests/test_commands.cpp` — transcribed verbatim from the brief; validates
  the real command table (order, filter parsing, required options, exactly-
  one-value-source, `--compress` validation incl. compiled-out codecs,
  `--limit` parsing, and that `set --help`'s compress help text lists only
  compiled-in codec names).
- `tests/cli_smoke.cmake` — transcribed verbatim from the brief; drives the
  built `rcedit.exe` through usage errors, `--version`/`--help`, `set`/
  `list`/`get`/`hexdump`, runtime errors, `--output` non-mutation, an
  optional compression round-trip (parameterized by `-DCODEC`), and
  `remove`.
- `tests/CMakeLists.txt` — added `test_commands.cpp` and
  `${CMAKE_SOURCE_DIR}/src/cli/commands.cpp` to `rcedit_tests`; added
  `commands` to `RCEDIT_TEST_GROUPS`; added the `cli_smoke` CTest (codec
  parameterized: `zstd` when `RCEDIT_ENABLE_ZSTD`, else `7z` when
  `RCEDIT_ENABLE_7Z`, else empty) and, when both codecs are enabled, a
  second `cli_smoke_7z` test with `-DCODEC=7z` and workdir `smoke7z`.
- `tests/main.cpp` — registered `RunCommandsTests` under group name
  `"commands"`.

One deviation from the brief's literal code was required to get it to
compile at all (not a design change, a syntax fix forced by R12(b)):

```cpp
// brief (works, because *args.Value(...) is not a bare identifier):
const fs::path path(std::wstring(*args.Value(L"value-path")));

// after R12(b) pulls the value into a named variable first, the identical
// pattern becomes the textbook "most vexing parse" (MSVC parses `path` as
// a function declaration returning fs::path, taking a parameter named
// `valuePath` of type "pointer to function returning wstring"):
const auto valuePath = args.Value(L"value-path");
if (!valuePath) { return kUsage; }
const fs::path path(std::wstring(*valuePath));   // <-- parsed as a function decl, not a variable!
```

Fixed with brace-init, which is not subject to the vexing parse:
```cpp
const fs::path path{std::wstring(*valuePath)};
```
This is purely a consequence of applying R12(b) as literally as possible
(guard-then-deref via a named variable) and was necessary for the file to
compile; no other line was changed beyond R12(a)/(b).

## TDD evidence

**RED** (`minimal-MinSizeRel`, before `commands.h`/`commands.cpp` existed):
```
cmake --build --preset minimal-MinSizeRel
...
CMake Error: Cannot find source file: S:/llm/rcedit/src/cli/commands.cpp
CMake Error at ...: No SOURCES given to target: rcedit_tests
CMake Generate step failed.
```
(test_commands.cpp, the CMakeLists wiring, and tests/main.cpp registration
were all in place at this point — only commands.h/.cpp were missing, so the
configure step failed exactly as the brief predicted.)

**GREEN** — after implementing `commands.h`/`commands.cpp` and `main.cpp`:

- `minimal-MinSizeRel`: configure, build, `ctest` — **11/11 passed**
  (version, error, encoding, resource_id, codec, engine, ops, args,
  commands, imports, cli_smoke).
- `no-7z-MinSizeRel`: configure, build, `ctest` — **11/11 passed** (same
  groups; `cli_smoke` ran with `CODEC=zstd` since 7z is off and zstd is on
  for this preset, exercising the compression round-trip branch).
- `no-zstd-MinSizeRel`: configure, build succeeded; **`ctest` 11/12
  passed, `imports` FAILED** — see Blocker below. `cli_smoke` itself
  passed (12th test), including the `sevenzip` unit-test group.
- `default-MinSizeRel`: configure, build succeeded; **`imports` FAILED**
  the same way. Did not run the rest of the `default` suite or
  `default-Debug` since the blocker is preset-config-level (7z enabled),
  not test-order-dependent, and would reproduce identically.

## Imports-check result (R5 matrix)

| Preset | Expected (brief) | Actual |
|---|---|---|
| `minimal` | `KERNEL32.dll` | `KERNEL32.dll` — matches |
| `no-7z` | `KERNEL32.dll` | `KERNEL32.dll` — matches |
| `no-zstd` | `KERNEL32.dll`, `OLEAUT32.dll` | `KERNEL32.dll`, `USER32.dll`, `OLEAUT32.dll` — **extra `USER32.dll`** |
| `default` | `KERNEL32.dll`, `OLEAUT32.dll` | `KERNEL32.dll`, `USER32.dll`, `OLEAUT32.dll` — **extra `USER32.dll`** |

`comsuppw.lib` was not needed — the link itself succeeds cleanly on every
preset (no unresolved `_com_issue_error` or any other unresolved symbol).
The problem is not a link failure; it's an unexpected DLL import that only
the `imports` CTest catches.

## Blocker: `USER32.dll` / `CharUpperW` imported whenever 7z is enabled

**Exact symbol**: `CharUpperW`, imported from `USER32.dll`.

**Root cause**: `external/vcpkg/buildtrees/7zip/src/.../CPP/Common/MyString.h`
defines (verbatim from the vendored 7-Zip SDK, at the time of writing
around line 178):
```cpp
inline wchar_t MyCharUpper(wchar_t c) throw()
{
  if (c < 'a') return c;
  if (c <= 'z') return (wchar_t)(c - 0x20);
  if (c <= 0x7F) return c;
  #ifdef _WIN32
    #ifdef _UNICODE
      return (wchar_t)(unsigned)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)(unsigned)c);
    #else
      return (wchar_t)MyCharUpper_WIN(c);
    #endif
  ...
}
```
rcedit's top-level `CMakeLists.txt` defines `UNICODE`/`_UNICODE` globally,
and the 7zip overlay port (`external/vcpkg_overlay_ports/7zip/CMakeLists.txt`,
line ~402) also compiles the `7zip`/`extras` targets with
`-DUNICODE -D_UNICODE`. So on this exact configuration, `MyCharUpper()`'s
`_WIN32 && _UNICODE` branch is the one compiled, and it calls `CharUpperW`
straight from the Win32 API (USER32), not through kernel32's
`LCMapStringEx`/similar. This is an inline function in a header, so it gets
baked into whatever translation unit calls it.

**How it's reached from rcedit.exe**: `src/core/codec_7z.cpp` (Task 7,
already on `master`) calls `sevenzip::EnsureInitialized()` then
`::CreateObject(&CLSID_CFormat7z, &IID_IInArchive, ...)` /
`archive->Open(...)` to open/create in-memory 7z archives. That is the
standard 7-Zip SDK format-registration/archive-open path
(`NArchive::N7z::Register()` plus the archive open/signature-scan
machinery), and it is what pulls in a case-insensitive string/name
comparison that resolves to `MyCharUpper()` → `CharUpperW`. This code path
was already present and unit-tested directly in Task 7
(`tests/test_sevenzip.cpp`, `tests/test_codec.cpp`), but those tests link
`rcedit_tests.exe`, which is **not** subject to the `/NODEFAULTLIB` +
import-allowlist policy — only `rcedit.exe` is. Task 10 is, as the task
description says, "the first point where the whole CLI runs end-to-end and
where the import-allowlist is a real check": it's the first time anything
in `rcedit.exe` actually calls `Get`/`Set`/`List`/etc. (which reach the 7z
codec through `codec.h`'s always-compiled `AvailableCodecNames`/
`ParseCodecName`/`FindCodec`/`IsCodecAvailable`, and through `ops.cpp`'s
codec detection/decompression), so this is the first build where the
linker actually pulls `codec_7z.cpp`'s object code (and therefore 7-Zip's
archive-open machinery) into `rcedit.exe` at all. `/OPT:REF` does not
remove it because it's genuinely reachable, not dead code.

**Presets affected**: `no-zstd` and `default` (both have
`RCEDIT_ENABLE_7Z=ON`). `minimal` and `no-7z` (both `RCEDIT_ENABLE_7Z=OFF`)
are clean — `KERNEL32.dll` only, exactly as the brief predicts, because
`codec_7z.cpp`/the 7-Zip library are not compiled in at all on those
presets.

**What I did NOT do**, per the ruling ("do NOT add any library to fix
it"): did not add `user32.lib` (or anything else) to
`src/cli/CMakeLists.txt`'s `target_link_libraries`/allowlist; did not
widen `tests/CMakeLists.txt`'s `rcedit_add_import_check` allowed-DLL list;
did not patch the vendored 7-Zip source or the overlay port's compile
flags (e.g. forcing the non-Unicode `MyCharUpper_WIN` path, or replacing
`CharUpperW` with a kernel32-based equivalent) to make the SDK avoid
`CharUpperW` — any of those would be a design decision (accepting USER32
in the allowlist vs. patching a vendored third-party library) beyond
"transcribe the brief."

**Options for a human decision** (not acted on):
1. Add `USER32.dll` to the R5 allowlist for 7z-enabled presets — a policy
   change to the "anti side-loading" design goal that needs explicit
   sign-off, since USER32 is a much larger attack surface than
   OLEAUT32/uuid.
2. Patch `external/vcpkg_overlay_ports/7zip/*.patch` (or add a new patch)
   so `MyCharUpper`'s `_UNICODE` branch uses a kernel32-only equivalent
   (e.g. `LCMapStringEx`) instead of `CharUpperW`, eliminating the USER32
   dependency at the source.
3. Change `codec_7z.cpp`'s use of the SDK (e.g. avoid the generic
   `CreateObject`/archive-registration path in favor of a narrower, direct
   LZMA2/7z-encoder entry point that doesn't touch the string-comparison
   machinery) — needs 7-Zip SDK expertise to confirm feasibility.

## Self-review / other findings

- The command table, option specs, validate/run handlers, and `wmain`
  logic all match the brief's given code exactly (character-for-character
  aside from the R12 insertions and the forced brace-init fix above).
- `AvailableCodecNames()`/`CodecName()`/`ParseCodecName()`/
  `IsCodecAvailable()` in `codec.h` are always compiled (not `#ifdef`-
  gated), matching brief expectations for `CompressHelp()` and
  `SetCompressValidation`'s compiled-out-codec messages.
- `tests/test_commands.cpp` passed unmodified from the brief on every
  preset that got that far (`minimal`, `no-7z`, and — as one of the 12
  CTest entries — `no-zstd`, before its `imports` test failed).
- The `cli_smoke` end-to-end script itself passed on every preset it ran
  on, including the `no-zstd` preset's compression round-trip via
  `-DCODEC=zstd`... actually `no-zstd` has zstd OFF, so `RCEDIT_SMOKE_CODEC`
  resolved to `7z` there (7z is ON) — confirms the codec parameterization
  logic in `tests/CMakeLists.txt` correctly falls back from zstd to 7z.
- Did not reach `cli_smoke_7z` (only registered when both codecs are
  enabled, i.e. on `default`) or `default-Debug` because `default`'s
  `imports` test already fails before those would be meaningful to
  evaluate; the blocker is orthogonal to configuration (Debug vs
  MinSizeRel) since it's about which object code gets pulled into the
  link, not optimization level.

## Files changed (uncommitted)

- `S:\llm\rcedit\src\cli\commands.h` (new)
- `S:\llm\rcedit\src\cli\commands.cpp` (new)
- `S:\llm\rcedit\src\cli\main.cpp` (replaced stub)
- `S:\llm\rcedit\src\cli\CMakeLists.txt` (added commands.h/.cpp to sources)
- `S:\llm\rcedit\tests\test_commands.cpp` (new)
- `S:\llm\rcedit\tests\cli_smoke.cmake` (new)
- `S:\llm\rcedit\tests\CMakeLists.txt` (added test_commands.cpp +
  src/cli/commands.cpp to rcedit_tests, `commands` to
  RCEDIT_TEST_GROUPS, `cli_smoke`/`cli_smoke_7z` CTest registration)
- `S:\llm\rcedit\tests\main.cpp` (registered `RunCommandsTests`)

No commit was made (git status still shows these as unstaged/untracked).
`clang-format -i` was not yet run on the touched files, pending resolution
of the blocker (formatting a file that may still need a source change for
the fix seemed premature).

## Recommendation

Stopping per R5 as instructed. This needs a decision from the task owner
on which of the three options above (or another) to take before Task 10
can be completed and committed. Once decided, the fix is scoped to either
`tests/CMakeLists.txt`'s import allowlist (option 1) or the 7-Zip overlay
port (option 2) or `src/core/codec_7z.cpp` (option 3) — no further changes
to `src/cli/commands.{h,cpp}`, `src/cli/main.cpp`, or the test files
created in this task should be needed.

---

## R22 follow-up: CharUpperW fixed, but a second USER32 caller surfaced (Debug only) — STOPPING again per the ruling

Per R22, I removed the `CharUpperW` dependency at the source instead of
widening the allowlist.

### The patch

Created `external/vcpkg_overlay_ports/7zip/mycharupper-no-user32.patch`:
```diff
diff --git a/CPP/Common/MyString.h b/CPP/Common/MyString.h
index ba9914e..3d82cee 100644
--- a/CPP/Common/MyString.h
+++ b/CPP/Common/MyString.h
@@ -175,7 +175,7 @@ inline wchar_t MyCharUpper(wchar_t c) throw()
   if (c <= 0x7F) return c;
   #ifdef _WIN32
     #ifdef _UNICODE
-      return (wchar_t)(unsigned)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)(unsigned)c);
+      return (wchar_t)towupper((wint_t)c);
     #else
       return (wchar_t)MyCharUpper_WIN(c);
     #endif
```
Generated against the pristine `.clean` checkout the port extracts (verified
with `git apply --check` / `git apply` against a fresh copy of that
checkout — applies cleanly, no fuzz). Added to
`external/vcpkg_overlay_ports/7zip/portfile.cmake`'s `PATCHES` list
(after `my-com.patch`) and bumped `external/vcpkg_overlay_ports/7zip/vcpkg.json`'s
`"port-version"` from 8 to 9 so the port's ABI hash changes.

### Rebuild confirmation

`cmake --preset no-zstd` (and separately `cmake --preset default`) both
showed vcpkg detecting the ABI change and rebuilding 7-Zip from source:
```
7zip:x64-windows-static@24.06#9 -- .../vcpkg_overlay_ports\7zip
Removing 1/2 7zip:x64-windows-static
Installing 2/2 7zip:x64-windows-static@24.06#9...
7zip:x64-windows-static@24.06#9 package ABI: f06c762c0bb86254c8ce...
Building 7zip:x64-windows-static@24.06#9...
-- Extracting source .../ip7z-7zip-24.06.tar.gz
-- Using source at .../buildtrees/7zip/src/24.06-ce6048c53e.clean
-- Building x64-windows-static-dbg
-- Building x64-windows-static-rel
Starting submission of 7zip:x64-windows-static@24.06#9 to 1 binary cache(s)...
```
(the `default` preset's reconfigure then hit that same ABI in the binary
cache rather than rebuilding a second time — expected, since it's the same
port version/patch set). Verified the patched line landed in the fresh
checkout: `CPP/Common/MyString.h:178` now reads
`return (wchar_t)towupper((wint_t)c);` instead of the `CharUpperW` call.

### Imports result after the patch (MinSizeRel, all four presets)

| Preset | imports result |
|---|---|
| `minimal` | `KERNEL32.dll` only — PASS |
| `no-7z` | `KERNEL32.dll` only — PASS |
| `no-zstd` | `KERNEL32.dll`, `OLEAUT32.dll` — PASS (no more USER32) |
| `default` | `KERNEL32.dll`, `OLEAUT32.dll` — PASS (no more USER32) |

Full suites on MinSizeRel, all green:
- `minimal-MinSizeRel`: 11/11
- `no-7z-MinSizeRel`: 11/11
- `no-zstd-MinSizeRel`: 12/12 (incl. `sevenzip`, `imports`, `cli_smoke`)
- `default-MinSizeRel`: 13/13 (incl. `sevenzip`, `imports`, `cli_smoke`,
  `cli_smoke_7z`)

### New blocker: `default-Debug`'s `imports` still fails — a *different* USER32 symbol

`ctest --preset default-Debug`: **12/13 passed, `imports` FAILED**:
```
Forbidden imports in S:/llm/rcedit/build/default/Debug/rcedit.exe:
USER32.dll (allowed: KERNEL32.dll;OLEAUT32.dll)
```
`dumpbin /imports` on `build\default\Debug\rcedit.exe` shows the imported
USER32 symbol is now **`CharPrevExA`** (not `CharUpperW` — that one is
confirmed gone in every config). This entry does not appear in any
MinSizeRel `rcedit.exe` (`no-zstd` or `default`) — it is Debug-only.

Root cause: `CPP/7zip/Archive/Common/ItemNameUtils.cpp:116`, function
`HasTailSlash`:
```cpp
bool HasTailSlash(const AString &name, UINT
  #if defined(_WIN32) && !defined(UNDER_CE)
    codePage
  #endif
  )
{
  if (name.IsEmpty())
    return false;
  char c;
    #if defined(_WIN32) && !defined(UNDER_CE)
    if (codePage != CP_UTF8)
      c = *CharPrevExA((WORD)codePage, name, name.Ptr(name.Len()), 0);
    else
    #endif
    {
      c = name.Back();
    }
  return (c == '/');
}
```
`CharPrevExA` is only called when `codePage != CP_UTF8`; rcedit's
`codec_7z.cpp` uses a single fixed ASCII entry name (`L"payload"`), so in
practice this branch is exactly as unreachable at runtime for rcedit as the
`CharUpperW` branch was — same class of argument the R22 ruling made for
the first fix. It shows up only in Debug and not MinSizeRel most likely
because the vendored port isn't compiled with function-level linking
(`/Gy`)/whatever COMDAT-folding behavior the optimized configs get, so
`/OPT:REF` can't discard this individual unreferenced function separately
from the rest of its translation unit's object code in the Debug variant
of the vcpkg-built static lib (`x64-windows-static-dbg`), while it can in
the `-rel` variant that MinSizeRel/Release/RelWithDebInfo link against.

**I did not patch this** — R22's stop condition is explicit: "If USER32
still appears after the rebuild (another `CharUpperW`/USER32 caller
exists) ... STOP and report the exact dumpbin output / symbol / error. Do
NOT add `user32.lib` and do NOT widen the allowlist." This is exactly that
situation, so I stopped short of a second, undirected patch and am
reporting back instead. **No commit has been made** — the working tree
has both the code from Task 10 and the `mycharupper-no-user32.patch` +
portfile + vcpkg.json changes, uncommitted, pending direction on
`CharPrevExA`.

A same-shaped follow-up patch (swap the `CharPrevExA` branch for the
existing `c = name.Back();` fallback the function already uses on the
non-Windows/`UNDER_CE` path, by the identical "provably unreachable for
rcedit's fixed-ASCII-entry, always-CP_UTF8 use" reasoning) would plausibly
fix this the same way, but I did not apply it without explicit sign-off,
per the ruling's stop condition.

### Files added/changed by this follow-up (also uncommitted)

- `S:\llm\rcedit\external\vcpkg_overlay_ports\7zip\mycharupper-no-user32.patch` (new)
- `S:\llm\rcedit\external\vcpkg_overlay_ports\7zip\portfile.cmake` (added the new patch to `PATCHES`)
- `S:\llm\rcedit\external\vcpkg_overlay_ports\7zip\vcpkg.json` (`port-version` 8 → 9)

---

## R23: exempt Debug from the imports allowlist (no further 7-Zip patch)

Per R23, `CharPrevExA` is release-dead (guarded by `if (codePage != CP_UTF8)`,
and rcedit is always CP_UTF8) and only survives un-inlined in Debug because
the un-optimized build lacks the COMDAT/`\`/OPT:REF` elision that removes it
from every release config. Patching it (or the ~10 other similarly-guarded
7-Zip `Char*` USER32 sites) would be whack-a-mole with no benefit to the
shipped binary, so instead of another 7-Zip patch, Debug is exempted from
the `imports` CTest — the allowlist protects the distributed artifact, and
Debug is dev-only.

### The two edits

1. `tests/CMakeLists.txt`, `rcedit_add_import_check`: added
   `"-DCONFIG=$<CONFIG>"` to the `imports` test's `COMMAND` args.
2. `tests/check_imports.cmake`: right after the existing
   `if(NOT DUMPBIN OR NOT EXE OR NOT DEFINED ALLOWED)` guard, added:
   ```cmake
   if(CONFIG STREQUAL "Debug")
       message(STATUS "imports check skipped for Debug build (dev-only, not the distributed artifact)")
       return()
   endif()
   ```
   Release configs (MinSizeRel, RelWithDebInfo, Release) are unaffected and
   remain strictly enforced.

### Final verification — full matrix, all green

MinSizeRel (strict, no USER32 anywhere):

| Preset | `imports` result |
|---|---|
| `minimal` | `KERNEL32.dll` (all allowed) |
| `no-7z` | `KERNEL32.dll` (all allowed) |
| `no-zstd` | `KERNEL32.dll;OLEAUT32.dll` (all allowed) |
| `default` | `KERNEL32.dll;OLEAUT32.dll` (all allowed) |

Full suite results:
- `minimal-MinSizeRel`: 11/11
- `no-7z-MinSizeRel`: 11/11
- `no-zstd-MinSizeRel`: 12/12 (incl. `sevenzip`, `imports`, `cli_smoke`)
- `default-MinSizeRel`: 13/13 (incl. `sevenzip`, `imports`, `cli_smoke`,
  `cli_smoke_7z`)
- `default-Debug`: **13/13** — `imports` now passes, verbose CTest output
  confirms it's the skip path, not a false pass:
  `-- imports check skipped for Debug build (dev-only, not the distributed artifact)`

Rebuilt `minimal-MinSizeRel` once more after `clang-format -i` on the touched
C++ files, to confirm formatting didn't break the build — still 11/11.

### Commit

`clang-format -i` applied to `src/cli/commands.h`, `src/cli/commands.cpp`,
`src/cli/main.cpp`, `tests/test_commands.cpp` (not the `.cmake` files or the
vendored `.patch`). Task 10 committed as one commit including: the CLI code
(commands table + `wmain`, with R12(a)/(b)), the tests
(`test_commands.cpp`, `cli_smoke.cmake`, CMakeLists/main.cpp wiring), the
R22 7-Zip port patch (`mycharupper-no-user32.patch` + `portfile.cmake` +
`vcpkg.json` port-version bump), and the R23 imports-infra changes
(`tests/CMakeLists.txt` + `tests/check_imports.cmake`).

Verified `git show --stat` on the commit lists only: `external/vcpkg_overlay_ports/7zip/{mycharupper-no-user32.patch,portfile.cmake,vcpkg.json}`,
`src/cli/{commands.h,commands.cpp,main.cpp,CMakeLists.txt}`,
`tests/{test_commands.cpp,cli_smoke.cmake,CMakeLists.txt,check_imports.cmake,main.cpp}`
— no `.superpowers/` paths.

---

## R24: allowlist USER32 (KnownDLL) instead of patching 7-Zip — revert R22 + R23

The user overrode R22/R23: `USER32.dll` is a Windows KnownDLL (loaded only
from the protected `\KnownDlls` list, not side-loadable from an
attacker-controlled directory) — security-equivalent to the `OLEAUT32.dll`
already allowlisted for 7z. Decision: allowlist USER32 for 7z-enabled
presets instead of avoiding the import.

### Reverted (both workarounds, on top of d7c152b)

- Deleted `external/vcpkg_overlay_ports/7zip/mycharupper-no-user32.patch`.
- `external/vcpkg_overlay_ports/7zip/portfile.cmake`: removed
  `mycharupper-no-user32.patch` from `PATCHES` (back to the original two:
  `add-functions-and-fixes-for-static-link.patch`, `my-com.patch`).
- `external/vcpkg_overlay_ports/7zip/vcpkg.json`: `port-version` 9 → 8.
- `tests/check_imports.cmake`: removed the `if(CONFIG STREQUAL "Debug")
  ... return() endif()` block and its comment (restored verbatim to its
  pre-Task-10 form, via `git checkout d6f5047 --`).
- `tests/CMakeLists.txt`: removed the `"-DCONFIG=$<CONFIG>"` arg from the
  `imports` test's `COMMAND`.

All three port/check-imports files were restored via
`git checkout d6f5047 -- <paths>` and diffed clean against that pre-Task-10
commit (zero drift). The patch file was `git rm`'d.

### Added (the actual R24 change)

`tests/CMakeLists.txt`, `rcedit_add_import_check`:
```cmake
    set(allowed "KERNEL32.dll")
    if(RCEDIT_ENABLE_7Z)
        list(APPEND allowed "OLEAUT32.dll" "USER32.dll")
    endif()
```
Everything else Task-10-specific in `tests/CMakeLists.txt` (test_commands.cpp
and src/cli/commands.cpp added to `rcedit_tests`, `commands` in
`RCEDIT_TEST_GROUPS`, the `cli_smoke`/`cli_smoke_7z` `add_test`s) is
untouched — confirmed via `git diff HEAD -- tests/CMakeLists.txt`, which
shows only the two intended hunks (the `USER32.dll` addition and the
`-DCONFIG` removal).

No C++ was touched by this follow-up, so `clang-format` was not re-run.

### Rebuild confirmation

Reconfiguring `no-zstd` and `default` after removing the patch showed vcpkg
reverting to the pristine, unpatched 7-Zip port:
```
7zip:x64-windows-static@24.06#8 -- .../vcpkg_overlay_ports\7zip
Removing 1/2 7zip:x64-windows-static
Installing 2/2 7zip:x64-windows-static@24.06#8...
7zip:x64-windows-static@24.06#8 package ABI: 649cad2bfe01f650eafb3fdfb49e808f796fdc9920dec94e3579e39e8f65a6d8
```
(port version back to `#8`, matching the pre-Task-10 `vcpkg.json`; a fresh
ABI hash confirms the config actually changed and 7-Zip was reinstalled,
not served stale from cache.)

### Final imports matrix (after R24)

| Preset / config | `imports` result |
|---|---|
| `minimal` (MinSizeRel) | `KERNEL32.dll` (all allowed) |
| `no-7z` (MinSizeRel) | `KERNEL32.dll` (all allowed) |
| `no-zstd` (MinSizeRel) | `KERNEL32.dll;USER32.dll;OLEAUT32.dll` (all allowed) |
| `default` (MinSizeRel) | `KERNEL32.dll;USER32.dll;OLEAUT32.dll` (all allowed) |
| `default` (Debug) | `KERNEL32.dll;USER32.dll;OLEAUT32.dll` (all allowed) — runs normally now, no skip |

Full suite results, all green:
- `minimal-MinSizeRel`: 11/11
- `no-7z-MinSizeRel`: 11/11
- `no-zstd-MinSizeRel`: 12/12 (incl. `sevenzip`, `imports`, `cli_smoke`)
- `default-MinSizeRel`: 13/13 (incl. `sevenzip`, `imports`, `cli_smoke`,
  `cli_smoke_7z`)
- `default-Debug`: 13/13 (same 13, `imports` no longer skipped — passes
  straightforwardly with the three allowed DLLs)

### Commit

Follow-up commit on top of `d7c152b`, message
`Allowlist USER32 (KnownDLL) instead of patching 7-Zip`. `git show --stat`
on the follow-up commit shows exactly: the deleted patch file, the two
reverted port files (`portfile.cmake`, `vcpkg.json`), `tests/check_imports.cmake`
reverted, and `tests/CMakeLists.txt` with the USER32 allowlist + `-DCONFIG`
removal — no `.superpowers/` paths.
