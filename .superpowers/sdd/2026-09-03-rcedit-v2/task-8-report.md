# Task 8 Report: Operations (List, Get, Set, Remove, Hexdump)

## Status: DONE

## What was implemented

- `src/core/ops.h` / `src/core/ops.cpp`: the operations layer on top of the Task 7 `ResourceEngine` and Task 4-6 codecs, transcribed from the task brief with no design deviations.
  - `List` — enumerate + filter by type/name, `DetectCodec` per payload, `contentSize` via `Codec::ContentSize` when the codec is available.
  - `Get` — resolves language via `ResolveLanguage`, reads, decompresses unless `raw` or `CodecId::None`; a detected-but-disabled codec propagates `errc::codec_disabled` through `FindCodec`'s `std::expected` error channel.
  - `Set` — rejects empty payload (`errc::empty_payload`), compresses first (before touching the target file) when `codec != CodecId::None`, resolves target via `PrepareTarget` (self-update guard, then copy-first when `output` is given), writes with lang defaulted to neutral (0) when unset, `Write`/`Commit`/`Discard`.
  - `Remove` — same target/copy logic as `Set`, resolves language after opening `ReadWrite` (matches the brief's "reads happen before the first write" ordering), `Remove`/`Commit`.
  - `Hexdump` — delegates to `Get` then `FormatHexdump`.
  - `ResolveLanguage` — exact lang used as-is; unset enumerates and collects candidates for matching type/name: none -> `resource_not_found`, neutral present -> neutral wins, exactly one -> that one, otherwise `ambiguous_language` with sorted `candidates` filled in.
  - `FormatHexdump` — 16 bytes/line, `{:08X}  ` prefix, hex pairs with the extra mid-gap space after the 8th byte, hex field padded to the constant 49-column `kHexWidth`, then `|ascii|`; `<empty>\n` for empty input; `limit` truncates with a trailing `... (N more bytes)` line.
  - `IsRunningExecutable` — compares against `GetModuleFileNameW` via `fs::equivalent`.
- `tests/test_ops.cpp`: transcribed verbatim from the brief, **except** the `HexdumpFormatting` first-line literal, which per Ruling R7 uses 30 spaces (not the brief's 31) between the last hex byte pair (`65`) and `|fixture|`, so the pipe aligns with the 16-byte-line literal at brief line 342. Verified programmatically (`grep`/`awk`) that the written file has exactly 30 spaces there, both before and after `clang-format -i`.
- Wired in: `tests/CMakeLists.txt` (added `test_ops.cpp` to `rcedit_tests` sources and `ops` to `RCEDIT_TEST_GROUPS`), `tests/main.cpp` (declared/registered `RunOpsTests` for group `"ops"`), `src/core/CMakeLists.txt` (added `ops.h`/`ops.cpp` to `RCEDIT_CORE_SOURCES`).

No production-code deviation from the brief was needed beyond R7 (test-literal only).

## TDD evidence

### RED (before implementation)

Command:
```
cmake --build --preset minimal-MinSizeRel
```
With `ops.h`/`ops.cpp` referenced in `src/core/CMakeLists.txt` (and `test_ops.cpp` wired into `tests/CMakeLists.txt`/`tests/main.cpp`) but the source files not yet created, the configure step failed:
```
CMake Error at external/vcpkg/scripts/buildsystems/vcpkg.cmake:666 (_add_library):
    ops.h
CMake Error at external/vcpkg/scripts/buildsystems/vcpkg.cmake:666 (_add_library):
ninja: error: rebuilding 'build-MinSizeRel.ninja': subcommand failed
```
This is the expected failure mode for this repo's CMake structure (source list references a nonexistent file) — equivalent to the brief's anticipated "`core/ops.h` not found" — confirming the test scaffolding could not build without the implementation.

### GREEN (after implementation)

Commands and results, `minimal-MinSizeRel` (both codecs compiled out — exercises `codec_disabled` paths):
```
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel --output-on-failure
```
```
100% tests passed, 0 tests failed out of 8
```
`ops` group in isolation (`rcedit_tests.exe ops <fixture.exe>`), minimal build — 15 cases (includes `ZstdDisabledOnSet/Get`, `SevenZipDisabledOnSet/Get`):
```
ops: 15 case(s), 0 failure(s)
```

`default-MinSizeRel` (both zstd and 7z compiled in — exercises the compress/decompress round trips):
```
cmake --preset default        # first configure: builds zstd + 7zip via vcpkg, ~2 min
cmake --build --preset default-MinSizeRel
ctest --preset default-MinSizeRel --output-on-failure
```
```
100% tests passed, 0 tests failed out of 9
```
`ops` group in isolation, default build — 13 cases (includes `ZstdRoundTrip`, `SevenZipRoundTrip` instead of the disabled variants):
```
ops: 13 case(s), 0 failure(s)
```

Re-ran both full suites after `clang-format -i` to confirm formatting did not change behavior — both still 100% pass (8/8 minimal, 9/9 default), `ops` still green on both.

## Files changed

- `S:\llm\rcedit\src\core\ops.h` (new)
- `S:\llm\rcedit\src\core\ops.cpp` (new)
- `S:\llm\rcedit\tests\test_ops.cpp` (new, R7-corrected 30-space `HexdumpFormatting` literal)
- `S:\llm\rcedit\src\core\CMakeLists.txt` (added ops.h/ops.cpp to RCEDIT_CORE_SOURCES)
- `S:\llm\rcedit\tests\CMakeLists.txt` (added test_ops.cpp source, `ops` test group)
- `S:\llm\rcedit\tests\main.cpp` (declared/registered RunOpsTests)

All four touched/created source files formatted with `clang-format -i` (repo's `.clang-format`, spaces-inside-parens/angles style) before commit.

## R7 confirmation

Applied as directed: the `HexdumpFormatting` test's first assertion uses exactly 30 spaces between `65` and `|fixture|` (`00000000  66 69 78 74 75 72 65` + 30 spaces + `|fixture|\n`), matching the golden `FormatHexdump` implementation's actual column alignment with the 16-byte-line literal at the same location. `FormatHexdump` itself was transcribed unchanged from the brief (constant `kHexWidth = 16*3+1 = 49`, one format string per line). Confirmed by running the test: `HexdumpFormatting` passes on both presets.

## Self-review findings

- Verified `FindCodec`'s `std::expected<Codec*, std::error_code>` error channel is threaded correctly in `MaybeDecompress` (Get/Hexdump path) and `Set`'s compress path — both return `codec.error()` directly, which for a compiled-out codec is `errc::codec_disabled` per `codec.h`'s contract.
- Verified `Set`'s ordering matches the brief: empty-payload check first, then compress (still against source `pe`/`data`, before any target file touched), then `PrepareTarget` (self-update guard + copy), then open/write/commit — so a doomed `Set` (empty payload, disabled codec, or self-update) never touches the output file or copies over it partially. Confirmed by `SetEmptyFails`, `DisabledCodecIsRejectedOnSet`, `SetRefusesRunningExecutable` all passing without leaving side effects (`ListAll(pe).size() == 1` checks).
- Verified `Remove`'s ordering (open ReadWrite first, then resolve language, matching the brief's comment "reads happen before the first write") — `ResolveLanguage`'s only engine call is `Enumerate`, which is a read, so it is safe before any `Write`/`Remove` call per the engine's documented lifecycle (`engine.h`: Enumerate/Read return `operation_not_permitted` only after a Write/Remove was issued).
- Confirmed the `ops` import-allowlist test (`imports`) still passes — `ops.cpp`'s only new Win32 dependency is `GetModuleFileNameW`, already covered by `KERNEL32.dll`.
- No new external dependencies, no unsafe casts beyond what the brief specifies, no TODOs left.

## Concerns

None. Both presets (`minimal-MinSizeRel`, `default-MinSizeRel`) build clean and all test suites (including `ops`, `imports`, and every prior task's group) pass with zero failures.

---

## Fix: R21 — Set error precedence (self_update before codec_disabled)

### Finding

Code review found that `Set` ran compression BEFORE target selection/the
`self_update` check, reversing the brief's stated precedence
(`empty → target → self_update → copy → compress`). Effect: `Set` with a
disabled codec whose target was the running executable returned
`errc::codec_disabled` instead of the mandated `errc::self_update`. No
corruption occurred (neither path writes), but the wrong refusal reason was
reported, and the case was untested.

### R21 ordering implemented

Per the coordinator's ruling, `PrepareTarget` was split into two helpers so
the self_update check and target selection could move ahead of compression
while the `pe`→`output` copy stays AFTER a successful compress (so a doomed
`Set` never leaves a stray unmodified copy at `output`):

- `SelectTarget(pe, output, target)` — picks `target` (`output` if given,
  else `pe`) and returns `errc::self_update` if `IsRunningExecutable(target)`.
  Does not copy.
- `CopyToOutput(pe, output)` — copies `pe` to `*output` with
  `overwrite_existing` when `output` is set; no-op otherwise. Never opens
  `pe` for writing.

`Set`'s new order in `src/core/ops.cpp`:
1. `data.empty()` → `errc::empty_payload`
2. `SelectTarget` → `errc::self_update` if the target is the running exe
3. compress (when `codec != CodecId::None`) → `FindCodec`/`Compress`; a
   disabled codec → `errc::codec_disabled` (still no side effect on disk)
4. `CopyToOutput` — `pe`→`output` copy, only reached after a successful
   compress
5. open `target` `ReadWrite`, `Write`, `Commit`

`Remove` was refactored to call the same two helpers in the same relative
order it always used (`SelectTarget` then `CopyToOutput`, before
`engine.Open`) — behavior-preserving; `Remove` has no compress step so R21
does not change its ordering, only its implementation via the shared
helpers.

### Covering test

Added `SetErrorPrecedenceSelfUpdateBeforeCodecDisabled` to
`tests/test_ops.cpp`: picks whichever codec is disabled in the current
build (`CodecId::Zstd` if `!RCEDIT_HAS_ZSTD`, else `CodecId::SevenZip`),
targets the running test executable (as `SetRefusesRunningExecutable`
does), and asserts `Set(...)` returns `errc::self_update` — not
`errc::codec_disabled`. Guarded by
`#if !defined(RCEDIT_HAS_ZSTD) || !defined(RCEDIT_HAS_7Z)`, mirroring how
`DisabledCodecIsRejectedOnSet/Get` are guarded, so it only exists on
presets with at least one codec compiled out (`minimal`, `no-7z`,
`no-zstd`); it does not exist on `default`, where both codecs are compiled
in and there is no disabled codec to exercise. Registered in `kCases`
under the same guard.

### RED (before the fix, with the new test added)

Reverted only `src/core/ops.cpp` to the pre-fix version (`git stash push
--keep-index -- src/core/ops.cpp`) while keeping the new test, then:
```
cmake --build --preset minimal-MinSizeRel
build\minimal\MinSizeRel\rcedit_tests.exe ops build\minimal\MinSizeRel\rcedit_fixture.exe
```
Output (excerpt):
```
FAIL S:\llm\rcedit\tests\test_ops.cpp:212: Set( *engine, self, kConfigNoLang, Bytes( "x" ), kDisabled, std::nullopt ) == errc::self_update
[ops] SetErrorPrecedenceSelfUpdateBeforeCodecDisabled
  -> FAILED
ops: 16 case(s), 1 failure(s)
```
Confirms the pre-fix code returned `codec_disabled` instead of
`self_update`, as predicted by the finding. `git stash pop` restored the
fix.

### GREEN (after the fix)

`minimal-MinSizeRel`:
```
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel --output-on-failure
```
```
100% tests passed, 0 tests failed out of 8
```
`ops` group in isolation — 16 cases, 0 failures, including
`SetErrorPrecedenceSelfUpdateBeforeCodecDisabled`:
```
ops: 16 case(s), 0 failure(s)
```

`default-MinSizeRel`:
```
cmake --build --preset default-MinSizeRel
ctest --preset default-MinSizeRel --output-on-failure
```
```
100% tests passed, 0 tests failed out of 9
```
`ops` group in isolation — 13 cases, 0 failures (the new precedence test is
compiled out here, as intended, since both codecs are enabled):
```
ops: 13 case(s), 0 failure(s)
```

Re-ran both full suites again after `clang-format -i src/core/ops.cpp
tests/test_ops.cpp` to confirm formatting changed nothing behaviorally —
still 8/8 (minimal) and 9/9 (default), `ops` still green on both.

### Files changed by this fix

- `S:\llm\rcedit\src\core\ops.cpp` — `PrepareTarget` split into
  `SelectTarget`/`CopyToOutput`; `Set` reordered per R21; `Remove` updated
  to call the two helpers (same relative order as before).
- `S:\llm\rcedit\tests\test_ops.cpp` — added
  `SetErrorPrecedenceSelfUpdateBeforeCodecDisabled` (guarded) and its
  `kCases` registration (guarded).
