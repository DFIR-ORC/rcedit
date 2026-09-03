# rcedit

Command-line tool to list, extract, set, remove and dump resources of Windows
PE files. Payloads can be stored 7z or zstd compressed; `get` and `hexdump`
decompress them transparently.

Static only: static CRT, 7-Zip and zstd linked statically, and the executable
imports nothing but `KERNEL32.dll` (plus `OLEAUT32.dll` and `USER32.dll`, both
KnownDLLs, when the 7z codec is built in). A test enforces that allowlist on
every build.

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
The `default` preset has `-Debug`, `-RelWithDebInfo` and `-MinSizeRel` build and
test presets; `no-7z`, `no-zstd` and `minimal` have `-MinSizeRel` only.
Options `RCEDIT_ENABLE_7Z`, `RCEDIT_ENABLE_ZSTD` and `RCEDIT_BUILD_TESTS` drive
the presets. Dependencies come from the vcpkg submodule; 7-Zip uses the
overlay port in `external/vcpkg_overlay_ports`.

## Design

See `docs/superpowers/specs/2026-09-03-rcedit-v2-design.md`. The Win32
resource API sits behind a `ResourceEngine` interface so a portable PE
engine can be added later.

## License

LGPL-2.1-or-later. Copyright ANSSI.
