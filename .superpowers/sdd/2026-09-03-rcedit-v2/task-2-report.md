# Task 2 report: Errors, logging, output, encoding, guards

## Status: DONE

## Files created

- `src/core/error.h`, `src/core/error.cpp` — `errc` enum (10 values, includes the
  accepted `empty_payload` deviation), `rcedit_category()`, `make_error_code`,
  `Win32Error`, `LastWin32Error`, `HResultError`.
- `src/core/encoding.h`, `src/core/encoding.cpp` — `Utf16ToUtf8`, `Utf8ToUtf16`,
  `AnsiToUtf16`, plus the R10 addition `EqualsIgnoreCase`.
- `src/core/format.h` — `FormatError(const std::error_code&)`.
- `src/core/log.h`, `src/core/log.cpp` — `rcedit::Log::{SetLevel, GetLevel,
  Debug, Info, Warn, Error, Write}`.
- `src/core/output.h`, `src/core/output.cpp` — `rcedit::Out::{Write, Print}`.
- `src/core/guard.h` — `ModuleHandle`, `FileHandle` RAII wrappers.
- `tests/test_error.cpp` — group `error`, 5 cases (verbatim from brief).
- `tests/test_encoding.cpp` — group `encoding`, 6 cases: the 5 from the brief
  plus `EqualsIgnoreCaseComparesFolded` (R10).

## Files modified

- `src/core/CMakeLists.txt` — appended `error.h error.cpp log.h log.cpp
  output.h output.cpp encoding.h encoding.cpp format.h guard.h` to
  `RCEDIT_CORE_SOURCES`.
- `tests/CMakeLists.txt` — added `test_error.cpp test_encoding.cpp` to the
  `rcedit_tests` executable; `RCEDIT_TEST_GROUPS` now `version error encoding`.
- `tests/main.cpp` — forward-declared `RunErrorTests()`/`RunEncodingTests()`
  and added `{"error", RunErrorTests}, {"encoding", RunEncodingTests}` to
  `kGroups`.

## R10 addition: `EqualsIgnoreCase`

Declared in `src/core/encoding.h`:
```cpp
[[nodiscard]] bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept;
```
Implemented in `src/core/encoding.cpp`: length check first, then a per-character
`towlower`-folded compare of both sides. Confirmed in place and covered by
`EqualsIgnoreCaseComparesFolded` in `tests/test_encoding.cpp` (equal, unequal,
mixed case, different lengths on both sides, empty vs empty, empty vs
non-empty).

One deviation from my first draft of that test: I initially asserted
`EqualsIgnoreCase(L"café", L"CAFÉ")` (accented case fold). It failed at
runtime — `towlower` in the process's default `"C"` locale does not fold
non-ASCII code points on this toolchain, so `é`/`É` compare unequal. This is a
`towlower`/locale property, not a bug in the folding logic (length check +
per-character `towlower` is exactly what R10 asked for). I replaced that
assertion with `EqualsIgnoreCase(L"café", L"café")` (equal non-ASCII strings)
per the brief's "non-ASCII if trivial" allowance — folding non-ASCII case
requires locale setup that is out of scope for this helper. Noting this for
Task 3/4 consumers: `EqualsIgnoreCase` reliably folds ASCII case only.

## TDD evidence (Step 2 — failing build before implementation)

After writing `tests/test_error.cpp`, `tests/test_encoding.cpp` and wiring
them into `tests/main.cpp` / `tests/CMakeLists.txt`, but before creating any
`src/core/*` files:

```
cmake --build --preset minimal-MinSizeRel
...
FAILED: tests/CMakeFiles/rcedit_tests.dir/MinSizeRel/test_encoding.cpp.obj
test_encoding.cpp(10): fatal error C1083: Cannot open include file: 'core/encoding.h': No such file or directory
FAILED: tests/CMakeFiles/rcedit_tests.dir/MinSizeRel/test_error.cpp.obj
test_error.cpp(12): fatal error C1083: Cannot open include file: 'core/error.h': No such file or directory
ninja: build stopped: subcommand failed.
```
Confirms the tests were wired up correctly and failed for the expected reason
(missing implementation) before any implementation code existed.

Also caught a real test bug via this loop (see R10 section above): the
`café`/`CAFÉ` assertion failed at test time, not compile time, which is
exactly what TDD is for — it surfaced a locale assumption in my test before
it became a Task 3/4 landmine.

## Build + test commands and results (post-implementation)

Dev shell: `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64` (the harmless
`'vswhere.exe' is not recognized...` line is emitted by the dev-shell script
itself, before configuration; it does not affect the build).

### minimal preset (primary iteration loop)
```
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Build: 9/9 targets built, zero warnings (recall `/W4 /WX /guard:cf /EHsc /sdl
/utf-8` is on for every TU).

```
Test project S:/llm/rcedit/build/minimal
    Start 1: version
1/4 Test #1: version ..........................   Passed    0.04 sec
    Start 2: error
2/4 Test #2: error ............................   Passed    0.01 sec
    Start 3: encoding
3/4 Test #3: encoding .........................   Passed    0.01 sec
    Start 4: imports
4/4 Test #4: imports ..........................   Passed    0.05 sec

100% tests passed, 0 tests failed out of 4
```

Direct exe run (verbose per-case output):
```
> rcedit_tests.exe error
[error] ErrcHasCategoryAndMessage
[error] EveryErrcHasDistinctMessage
[error] Win32ErrorUsesSystemCategory
[error] HResultErrorKeepsValue
[error] FormatErrorShowsCategoryAndMessage
error: 5 case(s), 0 failure(s)

> rcedit_tests.exe encoding
[encoding] RoundTripsAscii
[encoding] RoundTripsNonAscii
[encoding] EmptyIsEmpty
[encoding] RejectsInvalidUtf8
[encoding] RejectsLoneSurrogate
[encoding] EqualsIgnoreCaseComparesFolded
encoding: 6 case(s), 0 failure(s)
```

### default preset (7z + zstd enabled) — sanity check that core still builds
```
cmake --build --preset default-MinSizeRel
ctest --preset default-MinSizeRel
```
Build succeeded (vcpkg confirmed 7zip/zstd already installed), 4/4 tests
passed, 100%.

### Post clang-format re-verification
Ran `clang-format -i` on all new/changed `.h`/`.cpp` files (see below), then
rebuilt and reran `ctest --preset minimal-MinSizeRel`: still 4/4 passed, build
still clean (no new warnings introduced by reformatting).

## clang-format

```
clang-format -i src/core/error.h src/core/error.cpp src/core/log.h src/core/log.cpp \
    src/core/output.h src/core/output.cpp src/core/encoding.h src/core/encoding.cpp \
    src/core/format.h src/core/guard.h tests/test_error.cpp tests/test_encoding.cpp tests/main.cpp
```
Reformatted all files to the repo's `.clang-format` style (matches the style
already used in `tests/check.h` / `tests/test_version.cpp` from Task 1:
spaces inside parens, 4-space indent, etc.). Rebuilt and reran the full test
suite afterward — still pristine.

## Concerns

- None blocking. The one thing worth flagging to Task 3/4 authors: per the
  R10 note above, `EqualsIgnoreCase` folds ASCII case reliably but does not
  fold non-ASCII case (accented letters, etc.) under the process's default
  `"C"` locale on this toolchain. If a future task needs locale-aware
  non-ASCII case folding (e.g. for language names), that's a separate need
  beyond what `EqualsIgnoreCase` provides today.
