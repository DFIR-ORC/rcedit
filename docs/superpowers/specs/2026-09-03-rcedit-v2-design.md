# rcedit v2 design

Date: 2026-09-03
Status: approved in brainstorming, awaiting implementation plan

## Goal

Rewrite `rcedit`, a Windows command-line tool that lists, extracts, sets,
removes, and dumps PE resources. The rewrite replaces the whole current
tree. It drops CLI11 and spdlog, uses C++23 with `std::format` and
`std::print`, supports 7z and zstd compression of resource payloads (each
disableable at build time), consumes dependencies through vcpkg, and
isolates the Win32 resource API behind an engine interface so a portable
PE engine can be added later without touching the CLI.

Out of scope for this version: the portable PE engine, Linux builds,
manifest editing, JSON output.

## Decisions taken

| Topic | Decision |
|---|---|
| Location | This repository, rewritten in place, name stays `rcedit` |
| Engine | Interface plus Win32 implementation only |
| Commands | `list`, `get`, `set`, `remove`, `hexdump` |
| Decompression | `get`/`hexdump` auto-detect by magic and decompress by default, `--raw` gives stored bytes |
| Identifiers | Aliases (`RT_RCDATA`, `RCDATA`), `#n` numeric ids, anything else is a string name |
| CLI syntax | `--long value`, `-s value`, bare flags, positional PE path first |
| In-place edit | `set`/`remove` edit in place, `--output` copies then edits the copy |
| Portability | Windows only for now, `wmain`, wide strings outside byte payloads |
| Side-loading | `/NODEFAULTLIB` with explicit allowlist, kernel32 only, CTest import check |
| Linking | Static only: static CRT and static dependencies, no dynamic-runtime configuration |
| Tests | Hand-rolled test executable, no framework |
| Dependencies | vcpkg submodule, overlay ports and triplets from dfir-orc-forge, GitHub Actions CI |
| Architecture | `rcedit_core` static library plus thin `rcedit` executable |

## Repository layout

```
rcedit/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  external/vcpkg                          submodule, same tag as dfir-orc-forge
  external/vcpkg_overlay_ports/7zip       copied from dfir-orc-forge (static link patches)
  external/vcpkg_overlay_triplets/        x64-windows-static only
  src/core/                               rcedit_core static library
    resource_id.h/.cpp                    ResourceId, ResourceKey, aliases, parse/format
    engine.h                              ResourceEngine interface, MakeWin32Engine()
    engine_win32.cpp
    codec.h/.cpp                          Codec interface, Detect(), FindCodec()
    codec_7z.h/.cpp                       compiled when RCEDIT_ENABLE_7Z
    codec_zstd.h/.cpp                     compiled when RCEDIT_ENABLE_ZSTD
    sevenzip/                             in-memory streams, update callback, lib init
    ops.h/.cpp                            List, Get, Set, Remove, Hexdump
    error.h/.cpp                          rcedit_category, Win32 helpers
    log.h                                 Log::Debug/Info/Warn/Error over std::print
    format.h                              std::formatter for error_code, ResourceId, path
    guard.h                               RAII wrappers (module, file, update handle)
    encoding.h/.cpp                       UTF-16 <-> UTF-8
  src/cli/
    main.cpp                              wmain, dispatch, exit codes
    args.h/.cpp                           CommandSpec, OptionSpec, parser, help
    commands.h/.cpp                       one handler per command, validation
  tests/
    CMakeLists.txt
    check.h                               Check(), failure counter, Run()
    test_resource_id.cpp
    test_args.cpp
    test_codec.cpp
    test_engine.cpp                       integration on a fixture PE
    fixture/fixture.c                     tiny exe with no resources, built at test time
    check_imports.cmake                   dumpbin /imports allowlist
  .github/workflows/build.yml
  docs/superpowers/specs/
```

## Build

C++23. Compile options `/W4 /WX /guard:cf /EHsc /sdl`, definitions
`UNICODE`, `_UNICODE`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `RCEDIT_ENABLE_7Z` | ON | compile 7z codec, link `7zip::7zip` and `7zip::extras`, define `RCEDIT_HAS_7Z` |
| `RCEDIT_ENABLE_ZSTD` | ON | compile zstd codec, link `zstd::libzstd_static`, define `RCEDIT_HAS_ZSTD` |
| `RCEDIT_BUILD_TESTS` | ON | add `tests/` and `enable_testing()` |

There is no option for the runtime. `CMAKE_MSVC_RUNTIME_LIBRARY` is
fixed to `MultiThreaded$<$<CONFIG:Debug>:Debug>` and the only triplet is
`x64-windows-static`, so the CRT, 7-Zip, and zstd are always linked
statically. A configure with `BUILD_SHARED_LIBS` or a non-static triplet
is rejected with a fatal error.

### vcpkg

`vcpkg.json` declares no default dependencies and two features, `7z`
(port `7zip`) and `zstd` (port `zstd`). Presets pass
`VCPKG_MANIFEST_FEATURES` matching the two options, so a disabled codec is
neither built by vcpkg nor linked. vcpkg root, overlay ports, and overlay
triplets are set before `project()` as dfir-orc-forge does. The toolchain
file comes from the preset.

### Presets

Configure presets, all with `binaryDir` `build/<preset>`:

| Preset | Codecs | Triplet |
|---|---|---|
| `default` | 7z, zstd | `x64-windows-static` |
| `no-7z` | zstd | `x64-windows-static` |
| `no-zstd` | 7z | `x64-windows-static` |
| `minimal` | none | `x64-windows-static` |

Build presets: `default-Debug`, `default-RelWithDebInfo`,
`default-MinSizeRel`, `no-7z-MinSizeRel`, `no-zstd-MinSizeRel`,
`minimal-MinSizeRel`. Test presets mirror the build presets.

### Link policy

The `rcedit` executable links with `/NODEFAULTLIB` and an explicit list:
`kernel32.lib`, `libucrt.lib`, `libcmt.lib`, `libcpmt.lib`,
`libvcruntime.lib` (Debug: `libucrtd`, `libcmtd`, `libcpmtd`,
`libvcruntimed`). No user32, advapi32, version, shell32, or oleaut32.

7-Zip and zstd are linked statically through the overlay port and the
static triplet. 7-Zip's Windows sources call `SysAllocString`,
`SysFreeString`, and `VariantClear` for `BSTR` and `PROPVARIANT`
handling, so a build with `RCEDIT_ENABLE_7Z` also links `oleaut32.lib`
and `uuid.lib` and imports `OLEAUT32.dll`. OLEAUT32 is a KnownDLL: the
loader maps it from `\KnownDlls` and never searches the application
directory, so it cannot be side-loaded. The import allowlist is therefore
`KERNEL32.dll` for every preset, plus `OLEAUT32.dll` only when 7z is
enabled. Any other DLL, KnownDLL or not, fails the check; the fix is to
patch the dependency out in the overlay port, not to widen the list.

## Core library

### Identifiers

```cpp
using ResourceId = std::variant<uint16_t, std::wstring>;

struct ResourceKey {
    ResourceId              type;
    ResourceId              name;
    std::optional<uint16_t> lang;   // nullopt = not specified, see "Language resolution"
};

std::expected<ResourceId, std::error_code> ParseResourceId(std::wstring_view);
std::expected<ResourceId, std::error_code> ParseResourceType(std::wstring_view);
std::expected<uint16_t,   std::error_code> ParseLang(std::wstring_view);
std::wstring FormatResourceId(const ResourceId&);
std::wstring FormatResourceType(const ResourceId&);
```

Parsing rules:

- `#123` is numeric id 123. `#` followed by anything not a decimal
  integer in `[1, 65535]` is an error.
- For types only, a known alias maps to its numeric id. Aliases are the
  `RT_*` names with and without the `RT_` prefix, case-insensitive:
  CURSOR 1, BITMAP 2, ICON 3, MENU 4, DIALOG 5, STRING 6, FONTDIR 7,
  FONT 8, ACCELERATOR 9, RCDATA 10, MESSAGETABLE 11, GROUP_CURSOR 12,
  GROUP_ICON 14, VERSION 16, DLGINCLUDE 17, PLUGPLAY 19, VXD 20,
  ANICURSOR 21, ANIICON 22, HTML 23, MANIFEST 24.
- Anything else, including a bare decimal such as `101`, is a string
  name. Empty input is an error.
- Language accepts decimal and `0x` hex, range `[0, 65535]`.

Formatting: numeric types print their alias with the `RT_` prefix when
one exists, otherwise `#n`. Numeric names print `#n`. String names print
as-is.

Win32 boundary: `ToLpcwstr(const ResourceId&)` returns
`MAKEINTRESOURCEW(id)` or the string pointer; the enumeration callbacks
copy strings out before the callback returns, as OrcCapsule does.

### Engine interface

```cpp
enum class OpenMode { ReadOnly, ReadWrite };

struct ResourceEntry {
    ResourceKey key;
    uint32_t    size;
};

class ResourceEngine {
public:
    virtual ~ResourceEngine() = default;
    [[nodiscard]] virtual std::error_code Open(const std::filesystem::path&, OpenMode) = 0;
    [[nodiscard]] virtual std::error_code Enumerate(std::vector<ResourceEntry>&) = 0;
    [[nodiscard]] virtual std::error_code Read(const ResourceKey&, std::vector<uint8_t>&) = 0;
    [[nodiscard]] virtual std::error_code Write(const ResourceKey&, std::span<const uint8_t>) = 0;
    [[nodiscard]] virtual std::error_code Remove(const ResourceKey&) = 0;
    [[nodiscard]] virtual std::error_code Commit() = 0;
    virtual void Discard() = 0;
};

std::unique_ptr<ResourceEngine> MakeWin32Engine();
```

Win32 implementation:

- Reads and enumeration load the file with `LoadLibraryExW` and
  `LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE`.
  The target is never executed, its `DllMain` never runs, and 32-bit and
  64-bit images can both be inspected.
- `Enumerate` uses `EnumResourceTypesW`, `EnumResourceNamesW`,
  `EnumResourceLanguagesW`. `ERROR_RESOURCE_TYPE_NOT_FOUND`,
  `ERROR_RESOURCE_NAME_NOT_FOUND`, and `ERROR_RESOURCE_LANG_NOT_FOUND`
  during enumeration mean an empty result, not a failure.
- `Read` uses `FindResourceExW`, `LoadResource`, `SizeofResource`,
  `LockResource` and copies the bytes out.
- `Write` and `Remove` are queued. On the first write or remove in
  `ReadWrite` mode the engine frees the datafile module, then calls
  `BeginUpdateResourceW(path, FALSE)`. Each queued operation becomes an
  `UpdateResourceW` call with data or `nullptr`. `Commit` calls
  `EndUpdateResourceW(handle, FALSE)`. `Discard` calls
  `EndUpdateResourceW(handle, TRUE)`. The destructor discards and logs a
  warning if a session was left open.
- `Write` and `Remove` in `ReadOnly` mode return
  `rcedit_errc::read_only`.

### Codecs

```cpp
enum class CodecId { None, SevenZip, Zstd };

class Codec {
public:
    virtual ~Codec() = default;
    virtual CodecId Id() const = 0;
    virtual std::wstring_view Name() const = 0;   // L"7z", L"zstd"
    [[nodiscard]] virtual std::error_code Compress(std::span<const uint8_t>, std::vector<uint8_t>&) = 0;
    [[nodiscard]] virtual std::error_code Decompress(std::span<const uint8_t>, std::vector<uint8_t>&) = 0;
    // Size of the decompressed payload if the container records it.
    virtual std::optional<uint64_t> ContentSize(std::span<const uint8_t>) = 0;
};

CodecId DetectCodec(std::span<const uint8_t>);               // magic bytes, always available
std::expected<Codec*, std::error_code> FindCodec(CodecId);   // disabled codec -> codec_disabled
std::expected<CodecId, std::error_code> ParseCodecName(std::wstring_view); // "none", "7z", "zstd"
std::vector<std::wstring_view> AvailableCodecNames();        // for help text
```

Magic bytes: 7z `37 7A BC AF 27 1C`, zstd `28 B5 2F FD`. Detection is
compiled regardless of which codecs are enabled, so `list` can label a
payload even when it cannot decode it.

7z codec: rcedit's `InMemStream`, `OutMemStream`, and
`ArchiveUpdateCallback` ported to `std::span`, single entry named after
the resource name, level 9. In a static build the codec registers the 7z
format handler and the LZMA, BCJ, and copy codecs through the overlay
port's `Register()` hooks on first use. Decompression opens the archive
from memory and extracts the first entry to a vector. `ContentSize` reads
the first entry's `kpidSize`.

zstd codec: `ZSTD_compress` at level 19, `ZSTD_decompress` with
`ZSTD_getFrameContentSize` for the size. Frames without a content size
are decompressed with a streaming loop.

### Operations

```cpp
struct ListOptions { std::optional<ResourceId> type; std::optional<ResourceId> name; };
struct ListEntry   { ResourceKey key; uint32_t size; CodecId codec; std::optional<uint64_t> contentSize; };

[[nodiscard]] std::error_code List(ResourceEngine&, const fs::path&, const ListOptions&, std::vector<ListEntry>&);
[[nodiscard]] std::error_code Get(ResourceEngine&, const fs::path&, const ResourceKey&, bool raw, std::vector<uint8_t>&);
[[nodiscard]] std::error_code Set(ResourceEngine&, const fs::path&, const ResourceKey&, std::span<const uint8_t>, CodecId, const std::optional<fs::path>& output);
[[nodiscard]] std::error_code Remove(ResourceEngine&, const fs::path&, const ResourceKey&, const std::optional<fs::path>& output);
[[nodiscard]] std::error_code Hexdump(ResourceEngine&, const fs::path&, const ResourceKey&, bool raw, std::optional<size_t> limit, std::string& out);
```

- Language resolution, shared by `Get`, `Hexdump`, and `Remove`: when
  `key.lang` is set it is used as-is. When it is not set, the operation
  enumerates the languages of that type and name. Zero languages is
  `resource_not_found`; neutral (0) present picks neutral; otherwise
  exactly one language picks it; two or more is `ambiguous_language`,
  and the CLI lists them in the error message. `Set` with no language
  always writes neutral. The engine itself never resolves; it requires
  `key.lang` to be set and returns `invalid_language` otherwise.
- `Get` and `Hexdump` call `DetectCodec` on the stored bytes. If a codec is
  detected and `raw` is false, they call `FindCodec` and decompress. A
  detected but disabled codec is an error (`codec_disabled`), not a
  silent fallback to raw bytes.
- `Set` compresses when the codec is not `None`, then writes and commits.
- `Set` and `Remove` with `output` copy the source to `output` first
  (`std::filesystem::copy_file`, overwrite existing) and open the copy.
  The source is never opened for writing in that case.
- `Set` and `Remove` refuse to run on the current executable
  (`GetModuleFileNameW` compared with `std::filesystem::equivalent`), since
  `BeginUpdateResource` cannot update a running image.
- `Hexdump` output is 16 bytes per line: offset, hex, ASCII, like
  OrcCapsule's `FormatHexDump`.

### Errors

`std::error_code` is returned from every fallible function. No exceptions
cross the library boundary; the few places that can throw
(`std::bad_alloc`, `std::filesystem`) are caught at the operation level
and mapped.

```cpp
enum class rcedit_errc {
    invalid_identifier = 1,
    invalid_language,
    unknown_codec,
    codec_disabled,
    resource_not_found,
    ambiguous_language,
    read_only,
    self_update,
    corrupt_payload,
};
const std::error_category& rcedit_category();
std::error_code make_error_code(rcedit_errc);
std::error_code LastWin32Error();  // GetLastError() in std::system_category
```

### Logging and formatting

`log.h` provides `Log::SetLevel`, `Log::Debug`, `Log::Info`, `Log::Warn`,
`Log::Error`. Each is a variadic template over `std::wformat_string` that
writes to `stderr` through `std::print` with a level tag. Default level is
Info. Program output never goes through `Log`.

`format.h` specializes `std::formatter<wchar_t>` for `std::error_code`
(`{:#x}` plus message for system errors, category name plus message
otherwise), `ResourceId`, `ResourceKey`, and `std::filesystem::path`.

### Encoding

`encoding.h`: `Utf16ToUtf8` and `Utf8ToUtf16` over `WideCharToMultiByte`
and `MultiByteToWideChar` with `MB_ERR_INVALID_CHARS`, returning
`std::expected`. Used for `--value` and for writing UTF-8 program output.

## Command-line interface

### Grammar

```
rcedit [global options] <command> <pe_file> [command options]
```

- Options are `--long value`, `-s value`, or bare `--flag` / `-f`. No
  `=` or `:` joined forms, no `/opt` forms.
- `--` ends option parsing; remaining tokens are positional.
- The first positional after the command is the PE path. Extra
  positionals are a usage error.
- Global options may appear before or after the command: `--help`/`-h`,
  `--version`, `--verbose`/`-v`, `--quiet`/`-q`. `--verbose` sets the log
  level to Debug, `--quiet` to Error. Both together is a usage error.
- `rcedit` with no command prints usage and exits 2. `rcedit <command>
  --help` prints that command's usage and exits 0.

### Commands

| Command | Required | Optional |
|---|---|---|
| `list <pe>` | | `-t/--type`, `-n/--name` filters |
| `get <pe>` | `-t`, `-n`, `-o/--output` | `-l/--lang`, `--raw` |
| `set <pe>` | `-t`, `-n`, exactly one of `--value`, `--value-utf16`, `--value-path` | `-l`, `-c/--compress none\|7z\|zstd`, `-o/--output` |
| `remove <pe>` | `-t`, `-n` | `-l`, `-o/--output` |
| `hexdump <pe>` | `-t`, `-n` | `-l`, `--raw`, `--limit N` |

Semantics:

- `--lang` is optional. `set` without it writes neutral (0). `get`,
  `hexdump`, and `remove` without it follow the language resolution
  rule in the operations section; an `ambiguous_language` result is
  reported with exit code 1 and the candidate languages listed.
- `--value` stores the argument as UTF-8 bytes without a terminator.
  `--value-utf16` stores raw UTF-16LE bytes without a terminator.
  `--value-path` stores the file content.
- `--compress` help lists only the codecs compiled in. Naming a compiled
  out codec is a usage error that says the codec is disabled in this
  build.
- `--output` for `get` is the destination file, overwritten if present.
  For `set` and `remove` it is the path of the copied PE.
- `list` output is a fixed-width table to stdout:

```
TYPE          NAME        LANG    SIZE    CODEC   CONTENT
RT_RCDATA     CONFIG      0       12345   zstd    45678
RT_RCDATA     #101        1033    88      -       -
RT_MANIFEST   #1          1033    1024    -       -
```

  CONTENT is the decompressed size when the codec is available and the
  container records it, `?` when the codec is detected but disabled.

### Parser

```cpp
struct OptionSpec {
    std::wstring_view longName;    // without dashes
    wchar_t           shortName;   // 0 if none
    bool              takesValue;
    bool              required;
    std::wstring_view help;
};

struct CommandSpec {
    std::wstring_view               name;
    std::wstring_view               summary;
    std::span<const OptionSpec>     options;
    std::error_code               (*validate)(const ParsedArgs&, std::wstring& message);
    int                           (*run)(const ParsedArgs&);
};

struct ParsedArgs {
    const CommandSpec* command = nullptr;
    std::filesystem::path pePath;
    std::map<std::wstring_view, std::wstring> values;   // option -> value ("" for flags)
    bool help = false, version = false, verbose = false, quiet = false;
    [[nodiscard]] bool Has(std::wstring_view) const;
    [[nodiscard]] std::optional<std::wstring_view> Value(std::wstring_view) const;
};

std::expected<ParsedArgs, UsageError> Parse(std::span<const std::wstring> argv,
                                            std::span<const CommandSpec> commands);
std::wstring FormatUsage(std::span<const CommandSpec>);
std::wstring FormatCommandUsage(const CommandSpec&);
```

The generic loop: match a token against global options, then the current
command's `OptionSpec` table by long or short name; capture a value if
`takesValue`; reject unknown options, duplicate options, missing values,
and missing required options. The command's `validate` handles
cross-option rules such as "exactly one of the three value sources".
`Parse` performs no I/O and touches no Win32 API, so it is unit-tested
with plain vectors of strings. Help text is generated from the specs.

### Entry point and exit codes

`wmain` collects `argv` into `std::vector<std::wstring>`, calls `Parse`,
applies the log level, runs the command, and maps the result:

| Exit code | Meaning |
|---|---|
| 0 | success, or `--help` / `--version` |
| 1 | command failed at runtime (error printed to stderr) |
| 2 | usage error (message and the relevant usage printed to stderr) |

Diagnostics go to stderr, program output to stdout. stdout is switched to
UTF-8 text mode so names print correctly.

## Testing

### Unit tests

One executable `rcedit_tests` links `rcedit_core` and `src/cli/args.cpp`
and `src/cli/commands.cpp`. `tests/check.h` provides
`Check(bool, std::string_view what)`, a global failure counter, and a
`Run(name, fn)` wrapper that prints per-case results. The executable takes
a group name as its first argument so each group is one CTest:

- `resource_id`: alias parsing with and without `RT_`, case-insensitive;
  `#n` bounds; bare decimal stays a string; format round-trips; language
  decimal, hex, out of range.
- `args`: each command's required options; unknown option; duplicate
  option; missing value; `--` handling; positional count; global options
  before and after the command; `--verbose` with `--quiet`; `set` value
  source exclusivity; help output mentions every option in every spec;
  `--compress` help lists exactly the compiled codecs.
- `codec`: `DetectCodec` on the two magics and on random bytes;
  compress then decompress round-trip for each compiled codec on empty,
  small, and 1 MiB inputs; `ContentSize` matches; `FindCodec` on a
  compiled out codec returns `codec_disabled`.
- `engine`: integration on the fixture PE, see below.

### Fixture PE

`tests/fixture/fixture.c` is a minimal console program with no
resources. `tests/CMakeLists.txt` builds it as `rcedit_fixture` with the
same toolchain. The engine test copies it to a fresh temp directory per
case, then:

1. `list` is empty.
2. `set` a UTF-8 value, `list` shows one entry, `get` round-trips bytes.
3. `set` with each compiled codec, `list` shows the codec and content
   size, `get` decompresses, `get --raw` returns bytes starting with the
   magic.
4. `set` with `--output` leaves the source unchanged.
5. `hexdump` output has the expected first line.
6. `remove` deletes it, `list` is empty, `get` returns
   `resource_not_found`.
7. Same name in two non-neutral languages, then `get` without a language
   returns `ambiguous_language`; adding a neutral one makes it resolve
   to neutral.
8. `Write` in `ReadOnly` mode returns `read_only`.

### Import allowlist

`tests/check_imports.cmake` runs `dumpbin /imports <exe>` (found via
`CMAKE_LINKER` directory or `vswhere`), extracts every `.dll` line, and
fails unless the set is a subset of `{KERNEL32.dll}`, or of
`{KERNEL32.dll, OLEAUT32.dll}` when `RCEDIT_ENABLE_7Z` is on. Registered
as CTest `imports` for the `rcedit` target and runs on every preset and
configuration, Debug included.

### CI

`.github/workflows/build.yml`, runner `windows-2025`, triggers on push
to any branch, tags `v*`, and pull requests. Checkout with submodules.
Cache `build/<preset>/vcpkg_installed` keyed on `vcpkg.json` plus the
overlay port directory hash. Matrix over the configure presets `default`,
`no-7z`, `no-zstd`, `minimal`; each configures, builds its MinSizeRel
preset, and runs `ctest --output-on-failure`. `default` also builds and
tests Debug. On tags, the `default-MinSizeRel` job uploads
`rcedit.exe` and `rcedit.pdb` as workflow artifacts.

## Future work noted for design only

- Portable PE engine: implements `ResourceEngine` by parsing and rewriting
  the `.rsrc` section, enabling Linux builds. The CLI and ops are
  unaffected. At that point `wmain` becomes a Windows-only shim around a
  UTF-8 `main`.
- Manifest editing: a typed view over `RT_MANIFEST` id 1 built on
  `Get`/`Set`. It is why `ops` is separate from the CLI and why
  identifiers keep aliases.
- JSON output for `list`.
