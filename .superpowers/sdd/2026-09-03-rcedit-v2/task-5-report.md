# Task 5 report: zstd codec

## Files changed
- Created `src/core/codec_zstd.h` — `ZstdCodecImpl` declaration (final `Codec` subclass), per brief verbatim.
- Created `src/core/codec_zstd.cpp` — `Compress`/`Decompress`/`ContentSize`/`ZstdCodec()`, per brief verbatim, with the R14 comment added (see below).
- Modified `src/core/CMakeLists.txt` — added the `if(RCEDIT_ENABLE_ZSTD)` block after `add_library(rcedit_core ...)`: `find_package(zstd CONFIG REQUIRED)`, `target_sources` for the two new files, `target_link_libraries(rcedit_core PRIVATE zstd::libzstd_static)`.
- No changes needed to `tests/test_codec.cpp`, `tests/main.cpp`, or `tests/CMakeLists.txt` — Task 4 already added the `ZstdRoundTrip`/`ZstdRejectsEmpty`/`ZstdRejectsCorrupt` cases (guarded by `#ifdef RCEDIT_HAS_ZSTD`) inside the existing `codec` test group, and that group/executable/import-check wiring was already in place. Verified by reading both files before starting; git status confirms they are untouched.

Commit: `7d5531a` "Add zstd codec" (3 files changed, 225 insertions).

## R6 — resolved zstd CMake target
Inspected the actual installed vcpkg config after configuring `no-7z`:
`build/no-7z/vcpkg_installed/x64-windows-static/share/zstd/zstdTargets.cmake`

It exports **both** targets:
- `zstd::libzstd_static` — `add_library(... STATIC IMPORTED)`, the real static archive.
- `zstd::libzstd` — `add_library(... INTERFACE IMPORTED)` whose `INTERFACE_LINK_LIBRARIES` is `zstd::libzstd_static` (a compatibility shim for code written against the shared-lib name).

Per ruling R6 (static-only build), linked `zstd::libzstd_static` directly rather than through the shim. Documented this in a comment in `src/core/CMakeLists.txt` next to the `target_link_libraries` call. No fallback was needed — both names are exported by the installed package, so this was a preference choice, not a workaround.

## R14 — checksum comment
Added directly above the `ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_checksumFlag, 1)` call in `src/core/codec_zstd.cpp` (`Compress`, ~line 60 pre-format / see file):

```cpp
// R14: rcedit produces every frame it later reads back (payloads round-trip
// through this codec only), so the checksum is pure defense-in-depth: it
// catches accidental corruption in transit/storage and is verified
// transparently by ZSTD_decompressDCtx/ZSTD_decompressStream below.
```

Decompression uses `ZSTD_decompressDCtx` (content-size-known path) and `ZSTD_decompressStream` (unknown-size path), both of which verify the trailing XXH64 checksum automatically when present; no extra code was needed on the decompress side.

## TDD evidence
1. Configured `no-7z` (first time; vcpkg built `zstd:x64-windows-static@1.5.7` from source, ~a few seconds — already cached from a previous partial run in this session).
2. Built `no-7z-MinSizeRel` **before** adding the codec implementation (only `codec_zstd.h`/`.cpp` missing, i.e. `ZstdCodec()` undefined): got the expected link failure:
   ```
   test_codec.cpp.obj : error LNK2019: unresolved external symbol "class rcedit::Codec & __cdecl rcedit::ZstdCodec(void)" ...
   rcedit_core.lib(codec.cpp.obj) : error LNK2001: unresolved external symbol "class rcedit::Codec & __cdecl rcedit::ZstdCodec(void)"
   MinSizeRel\rcedit_tests.exe : fatal error LNK1120: 1 unresolved externals
   ```
3. Implemented `codec_zstd.h`/`.cpp` and the CMake wiring; rebuilt — link succeeded, `/W4 /WX /guard:cf /EHsc /sdl /utf-8` pristine (no warnings).
4. Ran `ctest --preset no-7z-MinSizeRel`: all 6 tests passed, including `codec` (which now covers `ZstdRoundTrip` — sizes 1B/100B/1MiB, `ZstdRejectsEmpty`, `ZstdRejectsCorrupt`) and `imports` (KERNEL32.dll only — static zstd link introduces no new DLL dependency).
5. Ran `clang-format -i` on the two new files (matches the existing `.clang-format`-driven style already used in `codec.h`/`codec.cpp`, e.g. spaced template angle brackets `std::span< const uint8_t >`), rebuilt from clean object files, reran tests — still pristine, still passing.

## Build + test commands and results

### `no-7z` (gating preset — zstd ON, 7z OFF)
```
cmake --preset no-7z
cmake --build --preset no-7z-MinSizeRel
ctest --preset no-7z-MinSizeRel
```
Result: build succeeds with zero warnings. Test output:
```
Test project S:/llm/rcedit/build/no-7z
1/6 Test #1: version ..........   Passed
2/6 Test #2: error ............   Passed
3/6 Test #3: encoding .........   Passed
4/6 Test #4: resource_id ......   Passed
5/6 Test #5: codec ............   Passed
6/6 Test #6: imports ..........   Passed
100% tests passed, 0 tests failed out of 6
```

### `minimal` (zstd OFF, 7z OFF) — confirm unaffected
```
cmake --preset minimal
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Result: `ninja: no work to do` (codec_zstd.* is excluded from this target's source list by `if(RCEDIT_ENABLE_ZSTD)`, so nothing changed for this preset — confirms isolation). All 6 tests passed (100%), same as before this task; `codec` group runs with the `#ifdef RCEDIT_HAS_ZSTD` cases compiled out, `imports` still only `KERNEL32.dll`.

### `default` preset
Not attempted, as instructed — it also enables 7z, whose `SevenZipCodec()` is undefined until Task 6; that is expected and out of scope here.

## Concerns
None. The implementation matches the brief verbatim (both header and source), the CMake target choice is the concretely-resolved static import (not a guess), the checksum flag is documented per R14, and both required presets build/test cleanly under `/W4 /WX`.

---

## Fix report (review finding, Important)

### Finding
`src/core/codec_zstd.cpp`, `ZstdCodecImpl::Decompress`, unknown-content-size streaming branch: the loop discarded each call's `ZSTD_decompressStream` return value except to check `ZSTD_isError`/`== 0`. That return is a *hint*, not an error code: `0` means the frame is fully reconstructed, `>0` means the decoder still expects more input. When the input buffer ran out (`in.pos == in.size`) while the last return was still non-zero — i.e. the frame was truncated mid-body — the loop simply exited and the function returned success with truncated output. This was unreached by `ZstdRejectsCorrupt` because `ZSTD_compress2` (used by `Compress`) always pledges/writes the content size, so all self-produced frames take the *known*-size branch (`ZSTD_decompressDCtx`), never the streaming branch.

### Fix
`src/core/codec_zstd.cpp`: hoisted `rc` out of the loop (`size_t rc = 1;` — a non-zero sentinel, safe because the loop always runs at least once for non-empty input), assign it each iteration instead of declaring it `const` inside the loop, and after the loop:
```cpp
if( rc != 0 ) {
    Log::Debug( L"Truncated zstd frame: input exhausted before frame end" );
    return make_error_code( errc::corrupt_payload );
}
```
Also expanded the comment above the loop to explain the hint-vs-error distinction, matching the fix's rationale.

### Covering tests added
`tests/test_codec.cpp`, inside the existing `#ifdef RCEDIT_HAS_ZSTD` block (so they compile/run only when zstd is enabled, same as the other Zstd* cases):

- **`CompressStreamingNoContentSize(input)`** — a helper, not a test case itself. Builds a *real* zstd frame that omits the embedded content size, using the raw streaming API directly (`#include <zstd.h>`, guarded by `RCEDIT_HAS_ZSTD`): `ZSTD_createCCtx`, `ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 0)`, then a single `ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end)` call with the whole input. `ZstdCodecImpl::Compress` can never produce such a frame (it always goes through `ZSTD_compress2`, which pledges the size), so this is the only way to reach the branch under test.
  - Note on a genuine dead end I hit and want on record: neither (a) never calling `ZSTD_CCtx_setPledgedSrcSize` nor (b) explicitly calling it with `ZSTD_CONTENTSIZE_UNKNOWN` was sufficient to suppress the header field — both produced a frame with a *known* content size anyway, confirmed by `ZstdCodec().ContentSize(packed).has_value()` being `true` in both cases when I tried them. Root cause: zstd's internal encoding of "unknown pledge" is `pledgedSrcSize + 1` wrapping to `0`, which is bit-for-bit the same internal state as "no pledge given" — and with the entire input handed over in one `ZSTD_e_end` call, zstd auto-infers and writes the size regardless. The only reliable way to suppress the header field, confirmed working, is `ZSTD_c_contentSizeFlag = 0`.
- **`ZstdStreamingFrameRoundTrip`** — builds a content-size-less frame via the helper (5000-byte pattern), asserts `ContentSize()` is `std::nullopt` (proving the streaming branch is what gets exercised), then asserts `Decompress` succeeds and the output equals the original input byte-for-byte.
- **`ZstdStreamingFrameRejectsTruncation`** — same frame, cut to half its length (`packed.size() / 2`, well before the end, with `CHECK(packed.size() > 100)` guarding that the cut point is meaningfully mid-frame), asserts `Decompress` returns exactly `errc::corrupt_payload`.

Both registered in `kCases[]` under the existing `#ifdef RCEDIT_HAS_ZSTD` guard.

`tests/CMakeLists.txt` also needed a small addition: `test_codec.cpp` now includes `<zstd.h>` directly, but `rcedit_core` links `zstd::libzstd_static` as `PRIVATE`, so its include-directory usage requirement doesn't propagate to `rcedit_tests`. Added:
```cmake
if(RCEDIT_ENABLE_ZSTD)
    find_package(zstd CONFIG REQUIRED)
    target_link_libraries(rcedit_tests PRIVATE zstd::libzstd_static)
endif()
```
right after `target_link_libraries(rcedit_tests PRIVATE rcedit_core)`, with a comment noting this doesn't affect the `imports` allowlist test (that test inspects `rcedit.exe` only, via `rcedit_add_import_check(rcedit)`, not `rcedit_tests.exe`).

### Regression-catching verification (before committing the fix)
To confirm the new test actually exercises the bug rather than being vacuous, I temporarily commented out the new `if( rc != 0 ) { ... }` guard (leaving everything else, including the test files, as fixed) and reran:
```
cmake --build --preset no-7z-MinSizeRel
.\build\no-7z\MinSizeRel\rcedit_tests.exe codec
```
Result with the guard disabled:
```
  FAIL S:\llm\rcedit\tests\test_codec.cpp:240: ec == errc::corrupt_payload
...
[codec] ZstdStreamingFrameRejectsTruncation
  -> FAILED
codec: 9 case(s), 1 failure(s)
```
This confirms the test fails without the fix. I then restored the real fix and rebuilt.

### Final build + test commands and results

`no-7z` preset (gating, zstd enabled):
```
cmake --build --preset no-7z-MinSizeRel
ctest --preset no-7z-MinSizeRel
```
```
Test project S:/llm/rcedit/build/no-7z
1/6 Test #1: version ..........   Passed
2/6 Test #2: error ............   Passed
3/6 Test #3: encoding .........   Passed
4/6 Test #4: resource_id ......   Passed
5/6 Test #5: codec ............   Passed
6/6 Test #6: imports ..........   Passed
100% tests passed, 0 tests failed out of 6
```
Direct run confirming all 9 `codec` cases including the 2 new ones:
```
.\build\no-7z\MinSizeRel\rcedit_tests.exe codec
[codec] DetectsMagics
[codec] NamesRoundTrip
[codec] FindCodecMatchesBuildFlags
[codec] AvailableNamesListOnlyCompiledCodecs
[codec] ZstdRoundTrip
[codec] ZstdRejectsEmpty
[codec] ZstdRejectsCorrupt
[codec] ZstdStreamingFrameRoundTrip
[codec] ZstdStreamingFrameRejectsTruncation
codec: 9 case(s), 0 failure(s)
```
Build was pristine under `/W4 /WX /guard:cf /EHsc /sdl /utf-8` (no warnings, in either the fixed rebuild or the temporary regression-check rebuild).

`minimal` preset (zstd disabled — confirm unaffected):
```
cmake --preset minimal
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
```
100% tests passed, 0 tests failed out of 6
```
(`test_codec.cpp`'s new helper/cases and the `<zstd.h>` include are compiled out entirely by `#ifdef RCEDIT_HAS_ZSTD`, and `tests/CMakeLists.txt`'s new zstd link is skipped by `if(RCEDIT_ENABLE_ZSTD)`, so this preset's build graph is untouched by this change — confirmed by the incremental build reporting no work needed for anything outside the changed files, and by `codec`/all 6 groups still passing.)

### Commit
`e861749` "Fix zstd streaming decompress silently truncating corrupt frames" — 3 files changed (`src/core/codec_zstd.cpp`, `tests/CMakeLists.txt`, `tests/test_codec.cpp`), 106 insertions, 2 deletions.

### Concerns
None remaining. The fix is minimal and targeted, the covering tests reach the exact branch the finding identified (verified both positively — pass with the fix — and negatively — fail without it), and both required presets (`no-7z` gating, `minimal` isolation) build and test cleanly.
