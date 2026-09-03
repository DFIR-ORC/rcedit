# Task 6 report: 7z codec

Status: DONE.

## What was recovered from the stash

`git stash pop` restored exactly the 12 expected paths with no conflict
(dropped `stash@{0}` = `ab0f6e6`): `src/core/codec_7z.{h,cpp}`, the six
`src/core/sevenzip/*` pairs (`sevenzip.h`, `in_mem_stream.{h,cpp}`,
`out_mem_stream.{h,cpp}`, `update_callback.{h,cpp}`,
`extract_callback.{h,cpp}`), and the `src/core/CMakeLists.txt` edit.

The recovered WIP was already substantially complete and, as it turned out,
already correct: it built and passed on the first `default` build with no code
changes required from me. Key observations:

- The prior implementer did **not** use the brief's literal
  `STDMETHOD(...)`/`STDMETHODIMP` spelling. It used the modern 7-Zip idiom
  instead: `Z7_IFACE_COM7_IMP(<iface>)` in the class bodies to *declare* the
  interface methods, and `Z7_COM7F_IMF(Class::Method(...))` in the `.cpp`
  files to *define* them. This is the correct, header-matching form for the
  vcpkg-installed 7-Zip 24.06 headers (see "COM header deviations" below) —
  the brief's `STDMETHOD` snippets would **not** have matched.
- The three rulings that touch the recovered code (R4, R8, R9) were all
  already applied by the prior implementer (verified below).
- `sevenzip.h` additionally carries a `#undef WIN32_LEAN_AND_MEAN` before the
  7-Zip includes (the project defines it globally, which would otherwise strip
  `PROPVARIANT`/`PROPID`/the OLE COM surface 7-Zip needs) and a
  `#pragma warning(push, 0)` wrap. `CMakeLists.txt` adds a
  `find_path(RCEDIT_7ZIP_ROOT_INCLUDE_DIR "7zip/7zip.h")` +
  `target_include_directories` so `<7zip/7zip.h>` resolves (the
  `7zip::7zip` package only exports `<prefix>/include/7zip/CPP`). Both are
  sound and were kept.

## What I had to add / fix

Nothing in the recovered source needed a correctness fix. My work was:

1. **Verified the test group already exists.** The `SevenZip*` cases the brief
   names (`SevenZipRoundTrip` — round-trip + `ContentSize`, `SevenZipRejectsEmpty`,
   `SevenZipRejectsCorrupt`) were written in Task 4 and are already committed in
   `tests/test_codec.cpp`, guarded by `#ifdef RCEDIT_HAS_7Z` and registered in
   the `codec` CTest group / `RunCodecTests`. No new `test_7z.cpp` group was
   required — the brief's "Test" line points at `tests/test_codec.cpp`, which
   is the authority over the earlier generic "add a test group" phrasing.
   `RoundTrip()`/`RejectsEmptyInput()`/`RejectsCorruptInput()` are shared with
   the zstd cases and exercise real 7z round-trips (sizes 1, 100, 1 MiB),
   `ContentSize == original size`, empty-input rejection, and corrupt-payload
   rejection.
2. **Confirmed the R5 link wiring** (already present from earlier tasks):
   `src/cli/CMakeLists.txt` links `oleaut32.lib uuid.lib` under
   `RCEDIT_ENABLE_7Z`; `tests/CMakeLists.txt` adds `OLEAUT32.dll` to the
   `imports` allowlist under the same guard.
3. **clang-format -i** on all 12 recovered source files (they were in a
   compact brace style; normalized to the project's `.clang-format`).
4. Removed a throwaway build helper script; it is not part of the commit.

## Files created / changed (this task's commit)

- `src/core/codec_7z.h`, `src/core/codec_7z.cpp`
- `src/core/sevenzip/sevenzip.h`
- `src/core/sevenzip/in_mem_stream.h`, `src/core/sevenzip/in_mem_stream.cpp`
- `src/core/sevenzip/out_mem_stream.h`, `src/core/sevenzip/out_mem_stream.cpp`
- `src/core/sevenzip/update_callback.h`, `src/core/sevenzip/update_callback.cpp`
- `src/core/sevenzip/extract_callback.h`, `src/core/sevenzip/extract_callback.cpp`
- `src/core/CMakeLists.txt` (7z `target_sources` + include-dir + link block)

## Build + test commands and results

Built inside the VS 18 BuildTools dev shell (MSVC 14.51). All four presets
configured and built clean under `/W4 /WX`; all CTests pass.

### default (7z + zstd — GATING)

```
cmake --preset default
cmake --build --preset default-MinSizeRel      # 8/8, clean /WX
ctest  --preset default-MinSizeRel
  1..6 version/error/encoding/resource_id/codec/imports  -> 100% passed (6/6)
rcedit_tests.exe codec -> 12 case(s), 0 failure(s)
  incl. SevenZipRoundTrip, SevenZipRejectsEmpty, SevenZipRejectsCorrupt
```

### no-zstd (7z only — GATING)

```
cmake --preset no-zstd
cmake --build --preset no-zstd-MinSizeRel      # clean /WX
ctest  --preset no-zstd-MinSizeRel             -> 100% passed (6/6)
rcedit_tests.exe codec -> 7 case(s), 0 failure(s)
  (4 common + 3 SevenZip; zstd cases compiled out)
```

### no-7z and minimal (7z compiled out — still pass)

```
cmake --preset no-7z    && cmake --build --preset no-7z-MinSizeRel    && ctest --preset no-7z-MinSizeRel    -> 100% passed (6/6)
cmake --preset minimal  && cmake --build --preset minimal-MinSizeRel  && ctest --preset minimal-MinSizeRel  -> 100% passed (6/6)
```

The `codec` group compiles out the `SevenZip*` cases via `#ifdef RCEDIT_HAS_7Z`
so 7z-disabled presets build and pass.

## imports output (KERNEL32 + OLEAUT32 only)

`dumpbin /imports` on the linked `rcedit.exe`:

- `build/default/MinSizeRel/rcedit.exe`  -> `KERNEL32.dll`
- `build/no-zstd/MinSizeRel/rcedit.exe`  -> `KERNEL32.dll`

Only `KERNEL32.dll` is imported — a subset of the R5 allowlist
(`KERNEL32.dll` + `OLEAUT32.dll`). As the brief predicted, the CLI does not
reference the 7z codec yet (that arrives with `ops` in Task 8), so `/OPT:REF`
drops the unused 7z objects and no `OLEAUT32` import is pulled in yet. The
`imports` CTest (which allows `KERNEL32.dll` and, under `RCEDIT_ENABLE_7Z`,
`OLEAUT32.dll`) passes in every preset.

## TDD evidence

The `SevenZip*` cases and `codec.cpp`'s `FindCodec(CodecId::SevenZip)` were
authored in Task 4 and reference `rcedit::SevenZipCodec()`, i.e. the failing
test existed before this task's implementation. To demonstrate concretely, I
temporarily renamed the `SevenZipCodec()` definition and rebuilt `no-zstd`:

```
test_codec.cpp.obj : error LNK2019: unresolved external symbol
  "class rcedit::Codec & __cdecl rcedit::SevenZipCodec(void)"
  referenced in function "...SevenZipRoundTrip(void)"
rcedit_core.lib(codec.cpp.obj) : error LNK2001: unresolved external symbol
  "class rcedit::Codec & __cdecl rcedit::SevenZipCodec(void)"
MinSizeRel\rcedit_tests.exe : fatal error LNK1120: 1 unresolved externals
```

This is exactly the brief's Step-1 expectation (unresolved external
`rcedit::SevenZipCodec`). Restoring the definition and rebuilding returns the
tests to green.

## R4 / R8 / R9 confirmation

- **R4** — applied. `HRESULT_WIN32_ERROR_NEGATIVE_SEEK` (an undefined macro) is
  not used anywhere. The single shared seek helper returns
  `HRESULT_FROM_WIN32( ERROR_NEGATIVE_SEEK )` on underflow
  (`src/core/sevenzip/sevenzip.h`).
- **R8** — applied. Both `InMemStream::Seek` and `OutMemStream::Seek` call one
  shared free function `rcedit::sevenzip::ComputeSeekPosition(offset,
  seekOrigin, currentPos, size, outPos)` in `sevenzip.h`; neither `.cpp`
  duplicates the switch/overflow logic (each just forwards, then stores
  `newPosition`).
- **R9** — applied. The single archive entry uses the fixed internal name
  `L"payload"` (`kEntryName` in `codec_7z.cpp`), carrying a code comment that
  this is an intentional deviation from the spec's "entry named after the
  resource name": `Codec::Compress` has no name parameter and `Decompress`
  always reads item index 0, so the name never surfaces.

## R5 — comsuppw.lib

**Not needed.** No `_com_issue_error` / `_com_*` unresolved symbols appeared;
`extras.lib` + `oleaut32.lib` + `uuid.lib` + the static CRT link cleanly.
`comsuppw.lib` was not added. Imports proof above confirms the exe still
imports only `KERNEL32.dll` (well within KERNEL32 + OLEAUT32).

## COM header signature deviations from the brief

The installed 7-Zip 24.06 headers (`.../vcpkg_installed/x64-windows-static/
include/7zip/CPP/7zip/IStream.h`, `.../Archive/IArchive.h`) define interfaces
through the `Z7_IFACEM_<iface>(x)` X-macro mechanism, so implementers use:

- **Declaration** (class body): `Z7_COM_UNKNOWN_IMP_n(...)` for the vtable/
  `QueryInterface`, then one `Z7_IFACE_COM7_IMP(<iface>)` per implemented
  interface (including inherited bases, e.g. `ISequentialInStream` *and*
  `IInStream`; `IProgress` *and* `IArchiveUpdateCallback`). This replaces the
  brief's per-method `STDMETHOD(...) override` list.
- **Definition** (`.cpp`): `Z7_COM7F_IMF(Class::Method(args)) { ... }` — which
  expands to the correct `Z7_COM7F_IMF`/`STDMETHODCALLTYPE` return type and
  calling convention — rather than the brief's `STDMETHODIMP Class::Method`.

The method *signatures themselves* (parameter types/order:
`Read(void*, UInt32, UInt32*)`, `Seek(Int64, UInt32, UInt64*)`,
`Write(const void*, UInt32, UInt32*)`, `SetSize(UInt64)`,
`GetUpdateItemInfo(UInt32, Int32*, Int32*, UInt32*)`,
`GetStream(UInt32, ISequentialInStream**)`,
`GetStream(UInt32, ISequentialOutStream**, Int32)`, etc.) match the brief and
the installed headers exactly. The prior WIP already used the correct macro
form, so no signature changes were needed.

---

# Fix report: unhandled std::length_error escaping OutMemStream (Important)

## Finding

`src/core/sevenzip/out_mem_stream.cpp` — `Write` and `SetSize` wrapped
`m_buffer.resize(...)` only in `catch (const std::bad_alloc&)`. `SetSize`
receives `newSize` as a raw `UInt64` taken from the archive header during
extraction (attacker/corrupt-controlled). A crafted 7z archive declaring an
unpacked size past `std::vector<uint8_t>::max_size()` makes `resize` throw
`std::length_error`, which was **not** caught here and **not** caught up the
chain (`SevenZipCodecImpl::Decompress` has no try/catch around
`archive->Extract(...)`). The exception escaped across the 7-Zip COM / library
boundary, violating the "no exceptions across a library boundary" constraint —
reachable via exactly the malicious-payload class this codec must defend
against.

## Fix

Broadened the catch in **both** `Write` and `SetSize` from
`catch (const std::bad_alloc&)` to `catch (const std::exception&)` (which
covers `std::length_error` as well as `bad_alloc`), still returning
`E_OUTOFMEMORY`. Replaced the now-unneeded `#include <new>` with
`#include <exception>`. A code comment at each site records that
`length_error` from an outsized declared size is the specific case being
contained.

Those two `resize` calls are the only attacker-size-driven allocations in the
7z decode path (InMemStream never allocates; the extract result vector grows
solely through `OutMemStream::Write`), so with the streams no longer throwing,
`Decompress` surfaces the failure as an ordinary `std::error_code`: the failing
`SetSize`/`Write` HRESULT makes `IInArchive::Extract` fail (and/or
`SetOperationResult != kOK`), and `Decompress` returns
`errc::corrupt_payload`. No try/catch was needed in `Decompress`.

## Covering test

New isolated translation unit `tests/test_sevenzip.cpp`, group `sevenzip`
(guarded by `RCEDIT_HAS_7Z`; kept separate from `test_codec.cpp` so the full
`<windows.h>` / 7-Zip headers stay out of the codec test unit). Two cases,
each wrapping the call in `try { ... } catch (const std::exception&)` and
failing the test if anything escapes:

- `OutMemStreamSetSizeRejectsOversize` — constructs `OutMemStream` over an
  empty vector, calls `SetSize(UINT64_MAX)`, asserts a FAILED HRESULT is
  returned (no throw) and the buffer stays empty.
- `OutMemStreamWriteRejectsOversize` — seeks the cursor to `INT64_MAX`, then
  `Write` of 1 byte (so the implicit grow `m_pos + size` exceeds
  `max_size()`), asserts FAILED HRESULT, `processedSize == 0`, buffer empty.

Registration (all guarded so 7z-disabled presets exclude it):
`tests/test_sevenzip.cpp` -> `target_sources(rcedit_tests ...)` under
`RCEDIT_ENABLE_7Z` (plus `find_package(7zip)` + `7zip::7zip`/`7zip::extras`
link and the root-include `find_path`, so `<7zip/7zip.h>` and
`7zip/CPP/...` resolve); `list(APPEND RCEDIT_TEST_GROUPS sevenzip)` under
`RCEDIT_ENABLE_7Z`; `RunSevenZipTests()` extern + `kGroups` entry in
`tests/main.cpp` under `#ifdef RCEDIT_HAS_7Z`.

## TDD evidence (fail-first, then pass)

Command: `& build\no-zstd\MinSizeRel\rcedit_tests.exe sevenzip`

- **Before the fix** (catch reverted to `bad_alloc` only), buggy build:
  the process aborts with exit code `-1073740791` (`0xC0000409`,
  fast-fail on an unhandled/escaping C++ exception) — no test lines printed,
  confirming the `length_error` escapes `OutMemStream` catastrophically rather
  than being contained.
- **After the fix**:
  ```
  [sevenzip] OutMemStreamSetSizeRejectsOversize
  [sevenzip] OutMemStreamWriteRejectsOversize
  sevenzip: 2 case(s), 0 failure(s)
  EXITCODE=0
  ```

## Regression / full-suite results (MSVC 14.51, /W4 /WX, MinSizeRel)

- `ctest --preset no-zstd-MinSizeRel` -> 100% passed, **7/7**
  (version, error, encoding, resource_id, codec, **sevenzip**, imports).
- `ctest --preset default-MinSizeRel`  -> 100% passed, **7/7** (same groups).
- `ctest --preset no-7z-MinSizeRel`    -> 100% passed, **6/6** (no `sevenzip`
  group — 7z compiled out, as intended).
- `ctest --preset minimal-MinSizeRel`  -> 100% passed, **6/6** (no `sevenzip`).

No `/WX` warnings or errors in any preset; both `rcedit.exe` and
`rcedit_tests.exe` build clean. clang-format applied to
`out_mem_stream.cpp`, `test_sevenzip.cpp`, and `main.cpp`.

Files changed by this fix: `src/core/sevenzip/out_mem_stream.cpp` (fix),
`tests/test_sevenzip.cpp` (new), `tests/main.cpp`, `tests/CMakeLists.txt`.
