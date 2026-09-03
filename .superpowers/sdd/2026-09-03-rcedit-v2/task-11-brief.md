### Task 11: CI workflow and README

**Files:**
- Create: `.github/workflows/build.yml`, `README.md`

**Interfaces:**
- Consumes: the four configure presets and their build/test presets from Task 1.

- [ ] **Step 1: Write the workflow**

`.github/workflows/build.yml`:
```yaml
#
# BEWARE: Do not use unofficial github actions as the version tag could be
# changed to a malicious commit. If necessary prefer to fork the action.
#
name: rcedit

on:
  push:
    branches: ["**"]
    tags: ["v[0-9]+.[0-9]+.[0-9]+*"]
  pull_request:

jobs:
  build:
    runs-on: windows-2025
    strategy:
      fail-fast: false
      matrix:
        preset: [default, no-7z, no-zstd, minimal]
        include:
          - preset: default
            extra_config: Debug

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: true

      - name: Cache vcpkg
        uses: actions/cache@v4
        with:
          path: build/${{ matrix.preset }}/vcpkg_installed
          key: vcpkg-${{ matrix.preset }}-${{ hashFiles('vcpkg.json', 'external/vcpkg_overlay_ports/**', 'external/vcpkg_overlay_triplets/**') }}-${{ hashFiles('.gitmodules') }}

      - name: Configure
        shell: cmd
        run: |
          echo C:\Program Files\Ninja>> %GITHUB_PATH%
          call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
          cmake --preset ${{ matrix.preset }}

      - name: Build MinSizeRel
        shell: cmd
        run: |
          call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
          cmake --build --preset ${{ matrix.preset }}-MinSizeRel

      - name: Test MinSizeRel
        shell: cmd
        run: |
          call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
          ctest --preset ${{ matrix.preset }}-MinSizeRel

      - name: Build and test Debug
        if: matrix.extra_config == 'Debug'
        shell: cmd
        run: |
          call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
          cmake --build --preset ${{ matrix.preset }}-Debug
          ctest --preset ${{ matrix.preset }}-Debug

      - name: Upload artifact
        if: matrix.preset == 'default'
        uses: actions/upload-artifact@v4
        with:
          name: rcedit-${{ github.ref_name }}
          path: |
            build/default/MinSizeRel/rcedit.exe
            build/default/MinSizeRel/rcedit.pdb
          retention-days: 7
```

Note: `RelWithDebInfo` and `MinSizeRel` both emit a PDB only if `/Zi` and `/DEBUG` are set; add `add_compile_options(/Zi)` and `add_link_options($<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF>)` to the top-level `CMakeLists.txt` in this task so the artifact step finds `rcedit.pdb`. Rebuild `default-MinSizeRel` locally and confirm `build/default/MinSizeRel/rcedit.pdb` exists.

- [ ] **Step 2: Write the README**

`README.md`:
````markdown
# rcedit

Command-line tool to list, extract, set, remove and dump resources of Windows
PE files. Payloads can be stored 7z or zstd compressed; `get` and `hexdump`
decompress them transparently.

Static only: static CRT, 7-Zip and zstd linked statically, and the executable
imports nothing but `KERNEL32.dll` (plus `OLEAUT32.dll`, a KnownDLL, when the
7z codec is built in). A test enforces that allowlist on every build.

## Usage

```
rcedit [global options] <command> <pe_file> [options]

Commands:
  list     List resources with size and detected compression
  get      Extract one resource to a file (decompressed unless --raw)
  set      Add or replace one resource
  remove   Delete one resource
  hexdump  Print one resource as hex and ASCII

Global options:
  -h, --help        Show this help, or a command's help
      --version     Show version
  -v, --verbose     Debug logging on stderr
  -q, --quiet       Errors only on stderr
```

Resource identifiers: `--type` accepts `RT_RCDATA`, `RCDATA`, `#10` or any
string name; `--name` accepts `#101` or a string (a bare number is a string);
`--lang` is decimal or `0x` hex and defaults to neutral. When `--lang` is
omitted on `get`, `remove` and `hexdump`, the neutral resource is used, then
the only existing language, otherwise the command fails and lists the
candidates.

Examples:

```
rcedit list app.exe
rcedit set app.exe -t RT_RCDATA -n CONFIG --value-path config.xml -c zstd
rcedit get app.exe -t RT_RCDATA -n CONFIG -o config.xml
rcedit hexdump app.exe -t RT_RCDATA -n CONFIG --limit 64
rcedit remove app.exe -t RT_RCDATA -n CONFIG -o app-without-config.exe
```

Exit codes: 0 success, 1 the command failed, 2 usage error.

## Build

Requirements: Visual Studio 2022 (MSVC with C++23), CMake 3.25+, Ninja. Run
from a x64 developer shell.

```
git clone --recurse-submodules <url> rcedit
cd rcedit
cmake --preset default
cmake --build --preset default-MinSizeRel
ctest --preset default-MinSizeRel
```

Presets: `default` (7z and zstd), `no-7z`, `no-zstd`, `minimal` (no codec).
Each has `-Debug`, `-RelWithDebInfo` (default only) and `-MinSizeRel` build
and test presets. Options `RCEDIT_ENABLE_7Z`, `RCEDIT_ENABLE_ZSTD` and
`RCEDIT_BUILD_TESTS` drive the presets. Dependencies come from the vcpkg
submodule; 7-Zip uses the overlay port in `external/vcpkg_overlay_ports`.

## Design

See `docs/superpowers/specs/2026-09-03-rcedit-v2-design.md`. The Win32
resource API sits behind a `ResourceEngine` interface so a portable PE
engine can be added later.

## License

LGPL-2.1-or-later. Copyright ANSSI.
````

- [ ] **Step 3: Verify locally and commit**

```powershell
cmake --build --preset default-MinSizeRel
Test-Path build/default/MinSizeRel/rcedit.pdb
ctest --preset default-MinSizeRel
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add CI workflow and README"
```

Expected: `Test-Path` prints `True`, all tests pass. The workflow itself can only be verified by pushing; note that in the task result.

---

## Self-review notes

- Spec coverage: layout and options (Task 1), link policy and import check (Tasks 1, 10), identifiers (3), engine (7), codecs (4, 5, 6), operations and language resolution (8), CLI grammar, parser and help (9, 10), exit codes and output streams (10), fixture and tests (1, 7, 10), CI (11). JSON output, Linux build and manifest editing are out of scope per the spec.
- Naming: the spec's `rcedit_errc` is `rcedit::errc` here; the spec's `validate` signature is simplified to `std::optional<std::wstring>`; `Hexdump` produces `std::wstring` rather than `std::string` so it flows through `Out::Print`. These are the only deliberate deviations.
- `Out::Write`/`Out::Print` and `Log::Write` are the only functions that touch stdout and stderr.
