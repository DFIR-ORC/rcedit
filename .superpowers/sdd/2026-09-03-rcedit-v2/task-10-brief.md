### Task 10: Commands, entry point, CLI smoke test

**Files:**
- Create: `src/cli/commands.h`, `src/cli/commands.cpp`, `tests/cli_smoke.cmake`
- Modify: `src/cli/main.cpp` (replace the stub), `src/cli/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/main.cpp`
- Test: `tests/test_commands.cpp` (validation of the real command table), `tests/cli_smoke.cmake` (end-to-end on the built exe)

**Interfaces:**
- Consumes: `Parse`, `FormatUsage`, `FormatCommandUsage`, `CommandSpec`, `OptionSpec`, `ParsedArgs`; all of `ops.h`, `codec.h`, `resource_id.h`, `encoding.h`, `format.h`, `log.h`, `output.h`, `version.h`.
- Produces: `std::span<const CommandSpec> rcedit::cli::Commands()` with commands `list`, `get`, `set`, `remove`, `hexdump` in that order. Exit codes: 0 ok, 1 runtime failure, 2 usage error.

Command table (from the spec):

| Command | Required | Optional |
|---|---|---|
| `list` | | `-t/--type`, `-n/--name` |
| `get` | `-t`, `-n`, `-o/--output` | `-l/--lang`, `--raw` |
| `set` | `-t`, `-n`, exactly one of `--value`, `--value-utf16`, `--value-path` | `-l`, `-c/--compress`, `-o/--output` |
| `remove` | `-t`, `-n` | `-l`, `-o/--output` |
| `hexdump` | `-t`, `-n` | `-l`, `--raw`, `--limit` |

Identifier, language, codec, and limit values are parsed in each command's `validate`, so a bad `--type` is a usage error (exit 2) with the command usage printed. `--compress` naming a compiled-out codec is a usage error saying so. The `run` handlers re-parse the validated values.

- [ ] **Step 1: Write failing tests for the command table**

`tests/test_commands.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include "cli/args.h"
#include "cli/commands.h"
#include "core/codec.h"

namespace rcedit::test {

namespace {

using namespace rcedit::cli;

std::expected<ParsedArgs, UsageError> ParseOf(std::initializer_list<const wchar_t*> tokens)
{
    std::vector<std::wstring> argv;
    for (const wchar_t* t : tokens)
    {
        argv.emplace_back(t);
    }
    return Parse(argv, Commands());
}

void FiveCommandsInOrder()
{
    const auto commands = Commands();
    CHECK(commands.size() == 5);
    CHECK(commands[0].name == L"list");
    CHECK(commands[1].name == L"get");
    CHECK(commands[2].name == L"set");
    CHECK(commands[3].name == L"remove");
    CHECK(commands[4].name == L"hexdump");
    for (const auto& c : commands)
    {
        CHECK(c.run != nullptr);
        CHECK(!c.summary.empty());
    }
}

void ListAcceptsFilters()
{
    CHECK(ParseOf({L"list", L"a.exe"}).has_value());
    CHECK(ParseOf({L"list", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG"}).has_value());
    auto bad = ParseOf({L"list", L"a.exe", L"-t", L"#0"});
    CHECK(!bad.has_value() && bad.error().message.find(L"--type") != std::wstring::npos);
}

void GetRequiresTypeNameOutput()
{
    CHECK(ParseOf({L"get", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG", L"-o", L"out.bin"}).has_value());
    CHECK(!ParseOf({L"get", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG"}).has_value());
    CHECK(ParseOf({L"get", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG", L"-o", L"o", L"-l", L"0x409", L"--raw"})
              .has_value());
    auto bad = ParseOf({L"get", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG", L"-o", L"o", L"-l", L"abc"});
    CHECK(!bad.has_value() && bad.error().message.find(L"--lang") != std::wstring::npos);
}

void SetRequiresExactlyOneValueSource()
{
    CHECK(ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG", L"--value", L"x"}).has_value());
    CHECK(ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG", L"--value-utf16", L"x"}).has_value());
    CHECK(ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG", L"--value-path", L"f"}).has_value());

    auto none = ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG"});
    CHECK(!none.has_value() && none.error().message.find(L"--value") != std::wstring::npos);
    auto two = ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"CONFIG", L"--value", L"x", L"--value-path", L"f"});
    CHECK(!two.has_value());
}

void SetCompressValidation()
{
    CHECK(ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C", L"--value", L"x", L"-c", L"none"}).has_value());

    auto unknown = ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C", L"--value", L"x", L"-c", L"lz4"});
    CHECK(!unknown.has_value() && unknown.error().message.find(L"lz4") != std::wstring::npos);

    auto zstd = ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C", L"--value", L"x", L"-c", L"zstd"});
    auto sevenZip = ParseOf({L"set", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C", L"--value", L"x", L"-c", L"7z"});
#ifdef RCEDIT_HAS_ZSTD
    CHECK(zstd.has_value());
#else
    CHECK(!zstd.has_value() && zstd.error().message.find(L"disabled") != std::wstring::npos);
#endif
#ifdef RCEDIT_HAS_7Z
    CHECK(sevenZip.has_value());
#else
    CHECK(!sevenZip.has_value() && sevenZip.error().message.find(L"disabled") != std::wstring::npos);
#endif
}

void RemoveAndHexdump()
{
    CHECK(ParseOf({L"remove", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C"}).has_value());
    CHECK(ParseOf({L"remove", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C", L"-o", L"b.exe"}).has_value());
    CHECK(!ParseOf({L"remove", L"a.exe", L"-t", L"RT_RCDATA"}).has_value());

    CHECK(ParseOf({L"hexdump", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C", L"--limit", L"32"}).has_value());
    auto bad = ParseOf({L"hexdump", L"a.exe", L"-t", L"RT_RCDATA", L"-n", L"C", L"--limit", L"x"});
    CHECK(!bad.has_value() && bad.error().message.find(L"--limit") != std::wstring::npos);
}

void CompressHelpListsCompiledCodecs()
{
    const std::wstring usage = FormatCommandUsage(Commands()[2]);
    CHECK(usage.find(L"none") != std::wstring::npos);
    const bool mentionsZstd = usage.find(L"zstd") != std::wstring::npos;
    const bool mentions7z = usage.find(L"7z") != std::wstring::npos;
#ifdef RCEDIT_HAS_ZSTD
    CHECK(mentionsZstd);
#else
    CHECK(!mentionsZstd);
#endif
#ifdef RCEDIT_HAS_7Z
    CHECK(mentions7z);
#else
    CHECK(!mentions7z);
#endif
}

constexpr TestCase kCases[] = {
    {"FiveCommandsInOrder", FiveCommandsInOrder},
    {"ListAcceptsFilters", ListAcceptsFilters},
    {"GetRequiresTypeNameOutput", GetRequiresTypeNameOutput},
    {"SetRequiresExactlyOneValueSource", SetRequiresExactlyOneValueSource},
    {"SetCompressValidation", SetCompressValidation},
    {"RemoveAndHexdump", RemoveAndHexdump},
    {"CompressHelpListsCompiledCodecs", CompressHelpListsCompiledCodecs},
};

}  // namespace

int RunCommandsTests()
{
    return RunGroup("commands", kCases);
}

}  // namespace rcedit::test
```

Register `RunCommandsTests` in `tests/main.cpp`; add `test_commands.cpp` and `${CMAKE_SOURCE_DIR}/src/cli/commands.cpp` to `rcedit_tests`; add `commands` to `RCEDIT_TEST_GROUPS`.

- [ ] **Step 2: Build to verify failure**

```powershell
cmake --build --preset minimal-MinSizeRel
```
Expected: `cli/commands.h` not found.

- [ ] **Step 3: Implement `commands.h/.cpp`**

`src/cli/commands.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <span>

#include "cli/args.h"

namespace rcedit::cli {

// list, get, set, remove, hexdump.
[[nodiscard]] std::span<const CommandSpec> Commands();

}  // namespace rcedit::cli
```

`src/cli/commands.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/commands.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>

#include "core/codec.h"
#include "core/encoding.h"
#include "core/engine.h"
#include "core/error.h"
#include "core/format.h"
#include "core/log.h"
#include "core/ops.h"
#include "core/output.h"
#include "core/resource_id.h"

namespace rcedit::cli {

namespace {

namespace fs = std::filesystem;

constexpr int kOk = 0;
constexpr int kFailure = 1;

// ---- shared option specs ----------------------------------------------------

constexpr OptionSpec kTypeRequired = {L"type", L't', true, true, L"TYPE", L"Resource type: RT_RCDATA, RCDATA, #10 or a name"};
constexpr OptionSpec kTypeOptional = {L"type", L't', true, false, L"TYPE", L"Only this resource type"};
constexpr OptionSpec kNameRequired = {L"name", L'n', true, true, L"NAME", L"Resource name: #101 or a string"};
constexpr OptionSpec kNameOptional = {L"name", L'n', true, false, L"NAME", L"Only this resource name"};
constexpr OptionSpec kLang = {L"lang", L'l', true, false, L"LANG", L"Language id, decimal or 0x hex (default: neutral)"};
constexpr OptionSpec kRaw = {L"raw", 0, false, false, L"", L"Do not decompress a 7z or zstd payload"};

std::wstring_view CompressHelp()
{
    static const std::wstring help = [] {
        std::wstring text = L"Compress the payload before storing it: ";
        const auto names = AvailableCodecNames();
        for (size_t i = 0; i < names.size(); ++i)
        {
            text += names[i];
            if (i + 1 < names.size())
            {
                text += L", ";
            }
        }
        return text;
    }();
    return help;
}

constexpr OptionSpec kListOptions[] = {kTypeOptional, kNameOptional};

constexpr OptionSpec kGetOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    {L"output", L'o', true, true, L"FILE", L"Destination file, overwritten"},
    kRaw,
};

const OptionSpec kSetOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    {L"value", 0, true, false, L"TEXT", L"Store TEXT as UTF-8 bytes (no terminator)"},
    {L"value-utf16", 0, true, false, L"TEXT", L"Store TEXT as UTF-16LE bytes (no terminator)"},
    {L"value-path", 0, true, false, L"FILE", L"Store the content of FILE"},
    {L"compress", L'c', true, false, L"CODEC", CompressHelp()},
    {L"output", L'o', true, false, L"FILE", L"Write to a copy of <pe_file> at FILE instead of in place"},
};

constexpr OptionSpec kRemoveOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    {L"output", L'o', true, false, L"FILE", L"Write to a copy of <pe_file> at FILE instead of in place"},
};

constexpr OptionSpec kHexdumpOptions[] = {
    kTypeRequired,
    kNameRequired,
    kLang,
    kRaw,
    {L"limit", 0, true, false, L"N", L"Show at most N bytes"},
};

// ---- value parsing shared by validate and run --------------------------------

std::expected<ResourceKey, std::wstring> ParseKey(const ParsedArgs& args, bool typeRequired)
{
    ResourceKey key;

    if (const auto type = args.Value(L"type"))
    {
        auto parsed = ParseResourceType(*type);
        if (!parsed)
        {
            return std::unexpected(std::format(L"invalid --type '{}'", *type));
        }
        key.type = std::move(*parsed);
    }
    else if (typeRequired)
    {
        return std::unexpected(L"missing required option '--type'");
    }

    if (const auto name = args.Value(L"name"))
    {
        auto parsed = ParseResourceName(*name);
        if (!parsed)
        {
            return std::unexpected(std::format(L"invalid --name '{}'", *name));
        }
        key.name = std::move(*parsed);
    }

    if (const auto lang = args.Value(L"lang"))
    {
        auto parsed = ParseLang(*lang);
        if (!parsed)
        {
            return std::unexpected(std::format(L"invalid --lang '{}'", *lang));
        }
        key.lang = *parsed;
    }

    return key;
}

std::expected<CodecId, std::wstring> ParseCompress(const ParsedArgs& args)
{
    const auto name = args.Value(L"compress");
    if (!name)
    {
        return CodecId::None;
    }
    const auto id = ParseCodecName(*name);
    if (!id)
    {
        return std::unexpected(std::format(L"unknown --compress codec '{}'", *name));
    }
    if (*id != CodecId::None && !IsCodecAvailable(*id))
    {
        return std::unexpected(std::format(L"--compress codec '{}' is disabled in this build", *name));
    }
    return *id;
}

std::expected<std::optional<size_t>, std::wstring> ParseLimit(const ParsedArgs& args)
{
    const auto text = args.Value(L"limit");
    if (!text)
    {
        return std::nullopt;
    }
    if (text->empty() || !std::ranges::all_of(*text, [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
    {
        return std::unexpected(std::format(L"invalid --limit '{}'", *text));
    }
    size_t value = 0;
    for (wchar_t c : *text)
    {
        value = value * 10 + static_cast<size_t>(c - L'0');
    }
    return value;
}

std::optional<fs::path> OutputPath(const ParsedArgs& args)
{
    const auto value = args.Value(L"output");
    if (!value)
    {
        return std::nullopt;
    }
    return fs::path(std::wstring(*value));
}

std::expected<std::vector<uint8_t>, std::error_code> ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
    {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    return data;
}

std::error_code WriteFile(const fs::path& path, std::span<const uint8_t> data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return std::make_error_code(std::errc::permission_denied);
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out)
    {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

// Explains an operation failure; lists candidate languages when ambiguous.
int Report(const std::error_code& ec, const fs::path& pe, const ResourceKey& key)
{
    if (ec == errc::ambiguous_language)
    {
        auto engine = MakeWin32Engine();
        ResourceKey resolved;
        std::vector<uint16_t> candidates;
        if (!engine->Open(pe, OpenMode::ReadOnly) && ResolveLanguage(*engine, key, resolved, candidates) == ec)
        {
            std::wstring list;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                list += std::format(L"{}{}", i ? L", " : L"", candidates[i]);
            }
            Log::Error(L"Resource {}/{} exists in several languages ({}), specify --lang",
                       FormatResourceType(key.type), FormatResourceName(key.name), list);
            return kFailure;
        }
    }

    Log::Error(L"Failed on '{}' [{}]", pe.wstring(), FormatError(ec));
    return kFailure;
}

// ---- validate -----------------------------------------------------------------

std::optional<std::wstring> ValidateList(const ParsedArgs& args)
{
    if (auto key = ParseKey(args, false); !key)
    {
        return key.error();
    }
    return std::nullopt;
}

std::optional<std::wstring> ValidateKeyOnly(const ParsedArgs& args)
{
    if (auto key = ParseKey(args, true); !key)
    {
        return key.error();
    }
    return std::nullopt;
}

std::optional<std::wstring> ValidateSet(const ParsedArgs& args)
{
    if (auto key = ParseKey(args, true); !key)
    {
        return key.error();
    }
    const int sources = (args.Has(L"value") ? 1 : 0) + (args.Has(L"value-utf16") ? 1 : 0) + (args.Has(L"value-path") ? 1 : 0);
    if (sources != 1)
    {
        return L"exactly one of --value, --value-utf16 or --value-path is required";
    }
    if (auto codec = ParseCompress(args); !codec)
    {
        return codec.error();
    }
    return std::nullopt;
}

std::optional<std::wstring> ValidateHexdump(const ParsedArgs& args)
{
    if (auto key = ParseKey(args, true); !key)
    {
        return key.error();
    }
    if (auto limit = ParseLimit(args); !limit)
    {
        return limit.error();
    }
    return std::nullopt;
}

// ---- run ------------------------------------------------------------------------

int RunList(const ParsedArgs& args)
{
    const auto key = ParseKey(args, false);
    ListOptions options;
    if (args.Has(L"type"))
    {
        options.type = key->type;
    }
    if (args.Has(L"name"))
    {
        options.name = key->name;
    }

    auto engine = MakeWin32Engine();
    std::vector<ListEntry> entries;
    if (const auto ec = List(*engine, args.pePath, options, entries))
    {
        return Report(ec, args.pePath, *key);
    }

    size_t typeWidth = 4;
    size_t nameWidth = 4;
    for (const auto& e : entries)
    {
        typeWidth = std::max(typeWidth, FormatResourceType(e.key.type).size());
        nameWidth = std::max(nameWidth, FormatResourceName(e.key.name).size());
    }

    Out::Print(L"{:<{}}  {:<{}}  {:>6}  {:>10}  {:<5}  {:>10}\n", L"TYPE", typeWidth, L"NAME", nameWidth, L"LANG", L"SIZE", L"CODEC", L"CONTENT");
    for (const auto& e : entries)
    {
        std::wstring codec = L"-";
        std::wstring content = L"-";
        if (e.codec != CodecId::None)
        {
            codec = std::wstring(CodecName(e.codec));
            if (e.contentSize)
            {
                content = std::format(L"{}", *e.contentSize);
            }
            else if (!IsCodecAvailable(e.codec))
            {
                content = L"?";
            }
        }
        Out::Print(L"{:<{}}  {:<{}}  {:>6}  {:>10}  {:<5}  {:>10}\n", FormatResourceType(e.key.type), typeWidth,
                   FormatResourceName(e.key.name), nameWidth, *e.key.lang, e.size, codec, content);
    }
    return kOk;
}

int RunGet(const ParsedArgs& args)
{
    const auto key = ParseKey(args, true);
    auto engine = MakeWin32Engine();
    std::vector<uint8_t> data;
    if (const auto ec = Get(*engine, args.pePath, *key, args.Has(L"raw"), data))
    {
        return Report(ec, args.pePath, *key);
    }

    const auto output = *OutputPath(args);
    if (const auto ec = WriteFile(output, data))
    {
        Log::Error(L"Failed to write '{}' [{}]", output.wstring(), FormatError(ec));
        return kFailure;
    }
    Log::Info(L"Wrote {} bytes to '{}'", data.size(), output.wstring());
    return kOk;
}

int RunSet(const ParsedArgs& args)
{
    const auto key = ParseKey(args, true);
    const auto codec = ParseCompress(args);

    std::vector<uint8_t> data;
    if (const auto value = args.Value(L"value"))
    {
        const auto utf8 = Utf16ToUtf8(*value);
        if (!utf8)
        {
            Log::Error(L"--value is not valid UTF-16 [{}]", FormatError(utf8.error()));
            return kFailure;
        }
        data.assign(utf8->begin(), utf8->end());
    }
    else if (const auto value16 = args.Value(L"value-utf16"))
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(value16->data());
        data.assign(bytes, bytes + value16->size() * sizeof(wchar_t));
    }
    else
    {
        const fs::path path(std::wstring(*args.Value(L"value-path")));
        auto content = ReadFile(path);
        if (!content)
        {
            Log::Error(L"Failed to read '{}' [{}]", path.wstring(), FormatError(content.error()));
            return kFailure;
        }
        data = std::move(*content);
    }

    auto engine = MakeWin32Engine();
    if (const auto ec = Set(*engine, args.pePath, *key, data, *codec, OutputPath(args)))
    {
        return Report(ec, args.pePath, *key);
    }
    Log::Info(L"Set {}/{} ({} bytes{})", FormatResourceType(key->type), FormatResourceName(key->name), data.size(),
              *codec == CodecId::None ? L"" : std::format(L", {} compressed", CodecName(*codec)));
    return kOk;
}

int RunRemove(const ParsedArgs& args)
{
    const auto key = ParseKey(args, true);
    auto engine = MakeWin32Engine();
    if (const auto ec = Remove(*engine, args.pePath, *key, OutputPath(args)))
    {
        return Report(ec, args.pePath, *key);
    }
    Log::Info(L"Removed {}/{}", FormatResourceType(key->type), FormatResourceName(key->name));
    return kOk;
}

int RunHexdump(const ParsedArgs& args)
{
    const auto key = ParseKey(args, true);
    const auto limit = ParseLimit(args);
    auto engine = MakeWin32Engine();
    std::wstring text;
    if (const auto ec = Hexdump(*engine, args.pePath, *key, args.Has(L"raw"), *limit, text))
    {
        return Report(ec, args.pePath, *key);
    }
    Out::Write(text);
    return kOk;
}

const CommandSpec kCommands[] = {
    {L"list", L"List resources with size and detected compression", kListOptions, ValidateList, RunList},
    {L"get", L"Extract one resource to a file (decompressed unless --raw)", kGetOptions, ValidateKeyOnly, RunGet},
    {L"set", L"Add or replace one resource", kSetOptions, ValidateSet, RunSet},
    {L"remove", L"Delete one resource", kRemoveOptions, ValidateKeyOnly, RunRemove},
    {L"hexdump", L"Print one resource as hex and ASCII", kHexdumpOptions, ValidateHexdump, RunHexdump},
};

}  // namespace

std::span<const CommandSpec> Commands()
{
    return kCommands;
}

}  // namespace rcedit::cli
```

- [ ] **Step 4: Replace `src/cli/main.cpp`**

```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include <exception>
#include <span>
#include <string>
#include <vector>

#include <windows.h>

#include "cli/args.h"
#include "cli/commands.h"
#include "core/encoding.h"
#include "core/log.h"
#include "core/output.h"
#include "core/version.h"

namespace {

constexpr int kUsageError = 2;

int Main(std::span<const std::wstring> argv)
{
    using namespace rcedit;
    using namespace rcedit::cli;

    const auto commands = Commands();
    auto parsed = Parse(argv, commands);
    if (!parsed)
    {
        Log::Error(L"{}", parsed.error().message);
        const auto* command = parsed.error().command;
        Log::Write(Log::Level::Error, command ? FormatCommandUsage(*command) : FormatUsage(commands));
        return kUsageError;
    }

    if (parsed->version)
    {
        Out::Print(L"rcedit {}\n", AnsiToUtf16(Version()));
        return 0;
    }
    if (parsed->help)
    {
        Out::Write(parsed->command ? FormatCommandUsage(*parsed->command) : FormatUsage(commands));
        return 0;
    }

    Log::SetLevel(parsed->verbose ? Log::Level::Debug : parsed->quiet ? Log::Level::Error : Log::Level::Info);
    return parsed->command->run(*parsed);
}

}  // namespace

int wmain(int argc, wchar_t* argv[])
{
    ::SetConsoleOutputCP(CP_UTF8);

    try
    {
        std::vector<std::wstring> args;
        for (int i = 1; i < argc; ++i)
        {
            args.emplace_back(argv[i]);
        }
        return Main(args);
    }
    catch (const std::exception& e)
    {
        rcedit::Log::Error(L"Unexpected exception: {}", rcedit::AnsiToUtf16(e.what()));
        return 1;
    }
}
```

Add `commands.h commands.cpp` to the `rcedit` sources in `src/cli/CMakeLists.txt`.

- [ ] **Step 5: Write the end-to-end smoke script and register it**

`tests/cli_smoke.cmake`:
```cmake
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
# Usage: cmake -DRCEDIT=<exe> -DFIXTURE=<exe> -DWORKDIR=<dir> [-DCODEC=zstd] -P cli_smoke.cmake
# Drives the built rcedit.exe through set/list/get/hexdump/remove on a copy of the fixture.

if(NOT RCEDIT OR NOT FIXTURE OR NOT WORKDIR)
    message(FATAL_ERROR "cli_smoke.cmake needs RCEDIT, FIXTURE and WORKDIR")
endif()

file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")
set(PE "${WORKDIR}/target.exe")
file(COPY_FILE "${FIXTURE}" "${PE}")

# run(<expected_exit> <var_for_stdout> args...)
function(run expected outvar)
    execute_process(
        COMMAND "${RCEDIT}" ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
    )
    if(NOT rc EQUAL expected)
        message(FATAL_ERROR "rcedit ${ARGN}\nexpected exit ${expected}, got ${rc}\nstdout:\n${out}\nstderr:\n${err}")
    endif()
    set(${outvar} "${out}" PARENT_SCOPE)
endfunction()

function(expect_match text pattern what)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${what}: expected to match '${pattern}', got:\n${text}")
    endif()
endfunction()

function(expect_no_match text pattern what)
    if(text MATCHES "${pattern}")
        message(FATAL_ERROR "${what}: expected no match for '${pattern}', got:\n${text}")
    endif()
endfunction()

# usage errors
run(2 out)
run(2 out list)
run(2 out list "${PE}" --bogus)
run(2 out set "${PE}" -t RT_RCDATA -n CONFIG)
run(0 out --version)
expect_match("${out}" "rcedit [0-9]+\\.[0-9]+\\.[0-9]+" "--version")
run(0 out --help)
expect_match("${out}" "Commands:" "--help")
run(0 out set --help)
expect_match("${out}" "--value-path" "set --help")

# baseline
run(0 out list "${PE}")
expect_match("${out}" "RT_RCDATA +FIXTURE +0 +7 +-" "list baseline")

# set / list / get / hexdump
run(0 out set "${PE}" -t RT_RCDATA -n CONFIG --value "<config/>")
run(0 out list "${PE}")
expect_match("${out}" "RT_RCDATA +CONFIG +0 +9 +-" "list after set")

run(0 out get "${PE}" -t RCDATA -n CONFIG -o "${WORKDIR}/config.bin")
file(READ "${WORKDIR}/config.bin" content)
if(NOT content STREQUAL "<config/>")
    message(FATAL_ERROR "get returned '${content}'")
endif()

run(0 out hexdump "${PE}" -t "#10" -n CONFIG)
expect_match("${out}" "00000000  3C 63 6F 6E 66 69 67 2F  3E .*\\|<config/>\\|" "hexdump")

run(0 out hexdump "${PE}" -t RT_RCDATA -n CONFIG --limit 4)
expect_match("${out}" "5 more bytes" "hexdump --limit")

# runtime errors exit 1
run(1 out get "${PE}" -t RT_RCDATA -n MISSING -o "${WORKDIR}/x.bin")
run(1 out list "${WORKDIR}/does-not-exist.exe")

# --output leaves the source untouched
run(0 out set "${PE}" -t RT_RCDATA -n OTHER --value-utf16 "ab" -o "${WORKDIR}/copy.exe")
run(0 out list "${PE}")
expect_no_match("${out}" "OTHER" "source after set --output")
run(0 out list "${WORKDIR}/copy.exe")
expect_match("${out}" "RT_RCDATA +OTHER +0 +4 +-" "copy after set --output")

# compression, when a codec is built in
if(CODEC)
    run(0 out set "${PE}" -t RT_RCDATA -n PACKED --value-path "${FIXTURE}" -c "${CODEC}")
    run(0 out list "${PE}" -n PACKED)
    expect_match("${out}" "PACKED +0 +[0-9]+ +${CODEC} +[0-9]+" "list packed")
    run(0 out get "${PE}" -t RT_RCDATA -n PACKED -o "${WORKDIR}/unpacked.bin")
    file(SHA256 "${FIXTURE}" expected_hash)
    file(SHA256 "${WORKDIR}/unpacked.bin" actual_hash)
    if(NOT expected_hash STREQUAL actual_hash)
        message(FATAL_ERROR "decompressed payload differs from the input")
    endif()
    run(0 out get "${PE}" -t RT_RCDATA -n PACKED -o "${WORKDIR}/raw.bin" --raw)
    file(SHA256 "${WORKDIR}/raw.bin" raw_hash)
    if(raw_hash STREQUAL expected_hash)
        message(FATAL_ERROR "--raw returned the decompressed payload")
    endif()
endif()

# remove
run(0 out remove "${PE}" -t RT_RCDATA -n CONFIG)
run(0 out list "${PE}")
expect_no_match("${out}" "CONFIG" "list after remove")
run(1 out remove "${PE}" -t RT_RCDATA -n CONFIG)

message(STATUS "cli smoke: OK")
```

In `tests/CMakeLists.txt`:
```cmake
set(RCEDIT_SMOKE_CODEC "")
if(RCEDIT_ENABLE_ZSTD)
    set(RCEDIT_SMOKE_CODEC "zstd")
elseif(RCEDIT_ENABLE_7Z)
    set(RCEDIT_SMOKE_CODEC "7z")
endif()

add_test(
    NAME cli_smoke
    COMMAND "${CMAKE_COMMAND}"
        "-DRCEDIT=$<TARGET_FILE:rcedit>"
        "-DFIXTURE=$<TARGET_FILE:rcedit_fixture>"
        "-DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/smoke/$<CONFIG>"
        "-DCODEC=${RCEDIT_SMOKE_CODEC}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake"
)
```
When both codecs are enabled, add a second test `cli_smoke_7z` identical except `-DCODEC=7z` and a distinct `WORKDIR` suffix `smoke7z`.

- [ ] **Step 6: Build and run everything on every preset**

```powershell
foreach ($p in "minimal","no-7z","no-zstd","default") {
    cmake --preset $p
    cmake --build --preset "$p-MinSizeRel"
    ctest --preset "$p-MinSizeRel"
}
cmake --build --preset default-Debug
ctest --preset default-Debug
```
Expected: every test passes on every preset. The `imports` test is now the real check because `rcedit.exe` finally links the codecs:
- `minimal`, `no-7z`: `KERNEL32.dll` only.
- `no-zstd`, `default`: `KERNEL32.dll` and `OLEAUT32.dll`.

If `imports` fails with another DLL, or the link fails on symbols outside kernel32/oleaut32/uuid (and comsuppw if needed), stop here and report the exact DLL or symbol names. Do not add libraries.

- [ ] **Step 7: Commit**

```powershell
clang-format -i src/cli/*.cpp src/cli/*.h tests/*.cpp
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add commands, entry point and CLI smoke test"
```

---

