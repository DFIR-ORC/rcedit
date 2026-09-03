### Task 9: Argument parser

**Files:**
- Create: `src/cli/args.h`, `src/cli/args.cpp`
- Modify: `src/cli/CMakeLists.txt`, `tests/CMakeLists.txt` (compile `src/cli/args.cpp` into `rcedit_tests`), `tests/main.cpp`
- Test: `tests/test_args.cpp`

**Interfaces:**
- Consumes: `Version()`.
- Produces (namespace `rcedit::cli`):
  ```cpp
  struct OptionSpec {
      std::wstring_view longName;   // without dashes
      wchar_t shortName;            // 0 if none
      bool takesValue;
      bool required;
      std::wstring_view valueName;  // shown in help, e.g. L"TYPE"
      std::wstring_view help;
  };
  struct ParsedArgs;
  struct CommandSpec {
      std::wstring_view name;
      std::wstring_view summary;
      std::span<const OptionSpec> options;
      std::optional<std::wstring> (*validate)(const ParsedArgs&);  // cross-option rules, message on failure
      int (*run)(const ParsedArgs&);                               // exit code
  };
  struct ParsedArgs {
      const CommandSpec* command = nullptr;
      std::filesystem::path pePath;
      std::map<std::wstring, std::wstring, std::less<>> values;   // longName -> value ("" for flags)
      bool help = false, version = false, verbose = false, quiet = false;
      bool Has(std::wstring_view longName) const;
      std::optional<std::wstring_view> Value(std::wstring_view longName) const;
  };
  struct UsageError { std::wstring message; const CommandSpec* command; };
  // argv excludes the program name.
  std::expected<ParsedArgs, UsageError> Parse(std::span<const std::wstring> argv, std::span<const CommandSpec> commands);
  std::wstring FormatUsage(std::span<const CommandSpec> commands);
  std::wstring FormatCommandUsage(const CommandSpec& command);
  ```
  Note: the spec sketched `validate` as returning `std::error_code` plus a message out-param; the plan uses `std::optional<std::wstring>` (message only) because a usage error has no other consumer.

Parser rules:
- Tokens: `--long`, `-s`, values as the next token (taken unconditionally, so a value may start with `-`), bare flags, `--` ends option parsing.
- Global options anywhere: `--help`/`-h`, `--version`, `--verbose`/`-v`, `--quiet`/`-q`.
- First positional is the command, second is the PE path, a third is an error.
- After the loop: `help` or `version` returns success immediately (no further checks). Then: missing command, missing PE path, verbose with quiet, duplicate option, unknown option, missing value, missing required option, then `validate`.
- Errors carry the command when known so `main` can print that command's usage.

- [ ] **Step 1: Write failing tests**

`tests/test_args.cpp`:
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

namespace rcedit::test {

namespace {

using namespace rcedit::cli;

constexpr OptionSpec kAlphaOptions[] = {
    {L"type", L't', true, true, L"TYPE", L"Resource type"},
    {L"name", L'n', true, false, L"NAME", L"Resource name"},
    {L"raw", 0, false, false, L"", L"Raw output"},
};

constexpr OptionSpec kBetaOptions[] = {
    {L"value", 0, true, false, L"TEXT", L"Inline value"},
    {L"value-path", 0, true, false, L"FILE", L"Value file"},
};

std::optional<std::wstring> ValidateBeta(const ParsedArgs& args)
{
    const int count = (args.Has(L"value") ? 1 : 0) + (args.Has(L"value-path") ? 1 : 0);
    if (count != 1)
    {
        return L"exactly one of --value or --value-path is required";
    }
    return std::nullopt;
}

int RunNoop(const ParsedArgs&)
{
    return 0;
}

const CommandSpec kCommands[] = {
    {L"alpha", L"Alpha command", kAlphaOptions, nullptr, RunNoop},
    {L"beta", L"Beta command", kBetaOptions, ValidateBeta, RunNoop},
};

std::expected<ParsedArgs, UsageError> ParseOf(std::initializer_list<const wchar_t*> tokens)
{
    std::vector<std::wstring> argv;
    for (const wchar_t* t : tokens)
    {
        argv.emplace_back(t);
    }
    return Parse(argv, kCommands);
}

void ParsesCommandPathAndOptions()
{
    auto r = ParseOf({L"alpha", L"a.exe", L"--type", L"RT_RCDATA", L"-n", L"CONFIG", L"--raw"});
    CHECK(r.has_value());
    CHECK(r->command == &kCommands[0]);
    CHECK(r->pePath == L"a.exe");
    CHECK(r->Value(L"type") == L"RT_RCDATA");
    CHECK(r->Value(L"name") == L"CONFIG");
    CHECK(r->Has(L"raw"));
    CHECK(!r->Has(L"missing"));
    CHECK(!r->Value(L"missing").has_value());
}

void OptionsMayPrecedeThePath()
{
    auto r = ParseOf({L"alpha", L"-t", L"X", L"a.exe"});
    CHECK(r.has_value() && r->pePath == L"a.exe" && r->Value(L"type") == L"X");
}

void GlobalsAnywhere()
{
    auto a = ParseOf({L"--verbose", L"alpha", L"a.exe", L"-t", L"X"});
    CHECK(a.has_value() && a->verbose && !a->quiet);
    auto b = ParseOf({L"alpha", L"a.exe", L"-t", L"X", L"-q"});
    CHECK(b.has_value() && b->quiet);
    auto c = ParseOf({L"alpha", L"a.exe", L"-t", L"X", L"-v", L"-q"});
    CHECK(!c.has_value());
}

void HelpAndVersionShortCircuit()
{
    auto a = ParseOf({L"--help"});
    CHECK(a.has_value() && a->help && a->command == nullptr);
    auto b = ParseOf({L"alpha", L"-h"});
    CHECK(b.has_value() && b->help && b->command == &kCommands[0]);
    auto c = ParseOf({L"--version"});
    CHECK(c.has_value() && c->version);
    auto d = ParseOf({L"beta", L"--help"});  // required checks and validate skipped
    CHECK(d.has_value() && d->help);
}

void MissingCommandOrPath()
{
    auto a = ParseOf({});
    CHECK(!a.has_value() && a.error().command == nullptr);
    CHECK(a.error().message.find(L"command") != std::wstring::npos);

    auto b = ParseOf({L"alpha", L"-t", L"X"});
    CHECK(!b.has_value() && b.error().command == &kCommands[0]);
    CHECK(b.error().message.find(L"pe_file") != std::wstring::npos);

    auto c = ParseOf({L"gamma", L"a.exe"});
    CHECK(!c.has_value() && c.error().message.find(L"gamma") != std::wstring::npos);
}

void ExtraPositionalIsAnError()
{
    auto r = ParseOf({L"alpha", L"a.exe", L"b.exe", L"-t", L"X"});
    CHECK(!r.has_value() && r.error().message.find(L"b.exe") != std::wstring::npos);
}

void UnknownDuplicateAndMissingValue()
{
    auto a = ParseOf({L"alpha", L"a.exe", L"-t", L"X", L"--bogus"});
    CHECK(!a.has_value() && a.error().message.find(L"--bogus") != std::wstring::npos);

    auto b = ParseOf({L"alpha", L"a.exe", L"-t", L"X", L"-t", L"Y"});
    CHECK(!b.has_value() && b.error().message.find(L"--type") != std::wstring::npos);

    auto c = ParseOf({L"alpha", L"a.exe", L"-t"});
    CHECK(!c.has_value() && c.error().message.find(L"value") != std::wstring::npos);

    auto d = ParseOf({L"alpha", L"a.exe", L"-t", L"X", L"-x"});
    CHECK(!d.has_value());

    auto e = ParseOf({L"--bogus", L"alpha", L"a.exe"});
    CHECK(!e.has_value() && e.error().command == nullptr);
}

void RequiredOptionEnforced()
{
    auto r = ParseOf({L"alpha", L"a.exe", L"-n", L"CONFIG"});
    CHECK(!r.has_value() && r.error().message.find(L"--type") != std::wstring::npos);
}

void ValuesMayStartWithDash()
{
    auto r = ParseOf({L"alpha", L"a.exe", L"-t", L"-weird"});
    CHECK(r.has_value() && r->Value(L"type") == L"-weird");
}

void DoubleDashEndsOptions()
{
    auto r = ParseOf({L"alpha", L"-t", L"X", L"--", L"-file.exe"});
    CHECK(r.has_value() && r->pePath == L"-file.exe");
}

void ValidateIsCalled()
{
    auto a = ParseOf({L"beta", L"a.exe"});
    CHECK(!a.has_value() && a.error().message.find(L"exactly one") != std::wstring::npos);
    auto b = ParseOf({L"beta", L"a.exe", L"--value", L"x", L"--value-path", L"f"});
    CHECK(!b.has_value());
    auto c = ParseOf({L"beta", L"a.exe", L"--value-path", L"f"});
    CHECK(c.has_value());
}

void UsageMentionsEverything()
{
    const std::wstring usage = FormatUsage(kCommands);
    CHECK(usage.find(L"alpha") != std::wstring::npos);
    CHECK(usage.find(L"Beta command") != std::wstring::npos);
    CHECK(usage.find(L"--verbose") != std::wstring::npos);
    CHECK(usage.find(L"--version") != std::wstring::npos);

    const std::wstring alpha = FormatCommandUsage(kCommands[0]);
    CHECK(alpha.find(L"-t, --type <TYPE>") != std::wstring::npos);
    CHECK(alpha.find(L"Resource type") != std::wstring::npos);
    CHECK(alpha.find(L"(required)") != std::wstring::npos);
    CHECK(alpha.find(L"--raw") != std::wstring::npos);
    CHECK(alpha.find(L"<pe_file>") != std::wstring::npos);
}

constexpr TestCase kCases[] = {
    {"ParsesCommandPathAndOptions", ParsesCommandPathAndOptions},
    {"OptionsMayPrecedeThePath", OptionsMayPrecedeThePath},
    {"GlobalsAnywhere", GlobalsAnywhere},
    {"HelpAndVersionShortCircuit", HelpAndVersionShortCircuit},
    {"MissingCommandOrPath", MissingCommandOrPath},
    {"ExtraPositionalIsAnError", ExtraPositionalIsAnError},
    {"UnknownDuplicateAndMissingValue", UnknownDuplicateAndMissingValue},
    {"RequiredOptionEnforced", RequiredOptionEnforced},
    {"ValuesMayStartWithDash", ValuesMayStartWithDash},
    {"DoubleDashEndsOptions", DoubleDashEndsOptions},
    {"ValidateIsCalled", ValidateIsCalled},
    {"UsageMentionsEverything", UsageMentionsEverything},
};

}  // namespace

int RunArgsTests()
{
    return RunGroup("args", kCases);
}

}  // namespace rcedit::test
```

Register `RunArgsTests` in `tests/main.cpp`. In `tests/CMakeLists.txt` add `test_args.cpp` and `${CMAKE_SOURCE_DIR}/src/cli/args.cpp` to `rcedit_tests`, and `args` to `RCEDIT_TEST_GROUPS`. The include path `${CMAKE_SOURCE_DIR}/src` already comes from `rcedit_core`'s public include directory, so `#include "cli/args.h"` resolves.

- [ ] **Step 2: Build to verify failure**

```powershell
cmake --build --preset minimal-MinSizeRel
```
Expected: `cli/args.h` not found.

- [ ] **Step 3: Implement `args.h/.cpp`**

`src/cli/args.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rcedit::cli {

struct OptionSpec
{
    std::wstring_view longName;   // without dashes
    wchar_t shortName;            // 0 if none
    bool takesValue;
    bool required;
    std::wstring_view valueName;  // shown in help when takesValue
    std::wstring_view help;
};

struct ParsedArgs;

struct CommandSpec
{
    std::wstring_view name;
    std::wstring_view summary;
    std::span<const OptionSpec> options;
    std::optional<std::wstring> (*validate)(const ParsedArgs& args);  // message on failure, may be null
    int (*run)(const ParsedArgs& args);                               // exit code
};

struct ParsedArgs
{
    const CommandSpec* command = nullptr;
    std::filesystem::path pePath;
    std::map<std::wstring, std::wstring, std::less<>> values;  // longName -> value ("" for flags)
    bool help = false;
    bool version = false;
    bool verbose = false;
    bool quiet = false;

    [[nodiscard]] bool Has(std::wstring_view longName) const { return values.contains(longName); }

    [[nodiscard]] std::optional<std::wstring_view> Value(std::wstring_view longName) const
    {
        const auto it = values.find(longName);
        if (it == values.end())
        {
            return std::nullopt;
        }
        return std::wstring_view(it->second);
    }
};

struct UsageError
{
    std::wstring message;
    const CommandSpec* command;  // known command, or null
};

// 'argv' excludes the program name. Pure: no I/O, no Win32.
[[nodiscard]] std::expected<ParsedArgs, UsageError>
Parse(std::span<const std::wstring> argv, std::span<const CommandSpec> commands);

[[nodiscard]] std::wstring FormatUsage(std::span<const CommandSpec> commands);
[[nodiscard]] std::wstring FormatCommandUsage(const CommandSpec& command);

}  // namespace rcedit::cli
```

`src/cli/args.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "cli/args.h"

#include <algorithm>
#include <format>

#include "core/encoding.h"
#include "core/version.h"

namespace rcedit::cli {

namespace {

struct GlobalFlag
{
    std::wstring_view longName;
    wchar_t shortName;
    std::wstring_view help;
    bool ParsedArgs::* field;
};

constexpr GlobalFlag kGlobals[] = {
    {L"help", L'h', L"Show this help, or a command's help", &ParsedArgs::help},
    {L"version", 0, L"Show version", &ParsedArgs::version},
    {L"verbose", L'v', L"Debug logging on stderr", &ParsedArgs::verbose},
    {L"quiet", L'q', L"Errors only on stderr", &ParsedArgs::quiet},
};

bool MatchesFlag(std::wstring_view token, std::wstring_view longName, wchar_t shortName)
{
    if (token.size() > 2 && token.starts_with(L"--"))
    {
        return token.substr(2) == longName;
    }
    return shortName != 0 && token.size() == 2 && token[0] == L'-' && token[1] == shortName;
}

const OptionSpec* FindOption(const CommandSpec& command, std::wstring_view token)
{
    for (const auto& option : command.options)
    {
        if (MatchesFlag(token, option.longName, option.shortName))
        {
            return &option;
        }
    }
    return nullptr;
}

const CommandSpec* FindCommand(std::span<const CommandSpec> commands, std::wstring_view name)
{
    for (const auto& command : commands)
    {
        if (command.name == name)
        {
            return &command;
        }
    }
    return nullptr;
}

std::unexpected<UsageError> Fail(std::wstring message, const CommandSpec* command)
{
    return std::unexpected(UsageError{std::move(message), command});
}

std::wstring OptionLabel(const OptionSpec& option)
{
    std::wstring label = option.shortName != 0 ? std::format(L"-{}, --{}", option.shortName, option.longName)
                                               : std::format(L"    --{}", option.longName);
    if (option.takesValue)
    {
        label += std::format(L" <{}>", option.valueName);
    }
    return label;
}

}  // namespace

std::expected<ParsedArgs, UsageError> Parse(std::span<const std::wstring> argv, std::span<const CommandSpec> commands)
{
    ParsedArgs args;
    bool endOfOptions = false;
    bool havePath = false;

    for (size_t i = 0; i < argv.size(); ++i)
    {
        const std::wstring_view token = argv[i];

        if (!endOfOptions && token == L"--")
        {
            endOfOptions = true;
            continue;
        }

        const bool looksLikeOption = !endOfOptions && token.size() > 1 && token.front() == L'-';
        if (looksLikeOption)
        {
            bool matchedGlobal = false;
            for (const auto& global : kGlobals)
            {
                if (MatchesFlag(token, global.longName, global.shortName))
                {
                    args.*(global.field) = true;
                    matchedGlobal = true;
                    break;
                }
            }
            if (matchedGlobal)
            {
                continue;
            }

            if (args.command == nullptr)
            {
                return Fail(std::format(L"unknown option '{}'", token), nullptr);
            }

            const OptionSpec* option = FindOption(*args.command, token);
            if (option == nullptr)
            {
                return Fail(std::format(L"unknown option '{}' for command '{}'", token, args.command->name), args.command);
            }
            if (args.Has(option->longName))
            {
                return Fail(std::format(L"option '--{}' given more than once", option->longName), args.command);
            }

            std::wstring value;
            if (option->takesValue)
            {
                if (i + 1 >= argv.size())
                {
                    return Fail(std::format(L"option '--{}' requires a value", option->longName), args.command);
                }
                value = argv[++i];
            }
            args.values.emplace(std::wstring(option->longName), std::move(value));
            continue;
        }

        // Positional.
        if (args.command == nullptr)
        {
            args.command = FindCommand(commands, token);
            if (args.command == nullptr)
            {
                return Fail(std::format(L"unknown command '{}'", token), nullptr);
            }
            continue;
        }
        if (!havePath)
        {
            args.pePath = std::wstring(token);
            havePath = true;
            continue;
        }
        return Fail(std::format(L"unexpected argument '{}'", token), args.command);
    }

    if (args.help || args.version)
    {
        return args;
    }

    if (args.command == nullptr)
    {
        return Fail(L"missing command", nullptr);
    }
    if (!havePath)
    {
        return Fail(L"missing <pe_file>", args.command);
    }
    if (args.verbose && args.quiet)
    {
        return Fail(L"--verbose and --quiet are mutually exclusive", args.command);
    }

    for (const auto& option : args.command->options)
    {
        if (option.required && !args.Has(option.longName))
        {
            return Fail(std::format(L"missing required option '--{}'", option.longName), args.command);
        }
    }

    if (args.command->validate != nullptr)
    {
        if (auto message = args.command->validate(args))
        {
            return Fail(std::move(*message), args.command);
        }
    }

    return args;
}

std::wstring FormatUsage(std::span<const CommandSpec> commands)
{
    std::wstring out = std::format(L"rcedit {} - Edit resources of Windows PE files\n\n", AnsiToUtf16(Version()));
    out += L"Usage: rcedit [global options] <command> <pe_file> [options]\n\n";

    out += L"Commands:\n";
    size_t width = 0;
    for (const auto& command : commands)
    {
        width = std::max(width, command.name.size());
    }
    for (const auto& command : commands)
    {
        out += std::format(L"  {:<{}}  {}\n", command.name, width, command.summary);
    }

    out += L"\nGlobal options:\n";
    for (const auto& global : kGlobals)
    {
        const std::wstring label = global.shortName != 0 ? std::format(L"-{}, --{}", global.shortName, global.longName)
                                                          : std::format(L"    --{}", global.longName);
        out += std::format(L"  {:<18}{}\n", label, global.help);
    }

    out += L"\nRun 'rcedit <command> --help' for the options of a command.\n";
    return out;
}

std::wstring FormatCommandUsage(const CommandSpec& command)
{
    std::wstring out = std::format(L"Usage: rcedit {} <pe_file> [options]\n\n{}\n", command.name, command.summary);

    if (command.options.empty())
    {
        return out;
    }

    size_t width = 0;
    for (const auto& option : command.options)
    {
        width = std::max(width, OptionLabel(option).size());
    }

    out += L"\nOptions:\n";
    for (const auto& option : command.options)
    {
        out += std::format(L"  {:<{}}  {}{}\n", OptionLabel(option), width, option.help, option.required ? L" (required)" : L"");
    }
    return out;
}

}  // namespace rcedit::cli
```

Add `args.h args.cpp` to the `rcedit` sources in `src/cli/CMakeLists.txt`.

- [ ] **Step 4: Build and run the tests**

```powershell
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Expected: `args` passes.

- [ ] **Step 5: Commit**

```powershell
clang-format -i src/cli/*.cpp src/cli/*.h tests/*.cpp
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add hand-rolled argument parser with generated help"
```

---

