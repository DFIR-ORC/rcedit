### Task 2: Errors, logging, output, encoding, guards

**Files:**
- Create: `src/core/error.h`, `src/core/error.cpp`, `src/core/log.h`, `src/core/log.cpp`, `src/core/output.h`, `src/core/output.cpp`, `src/core/encoding.h`, `src/core/encoding.cpp`, `src/core/format.h`, `src/core/guard.h`
- Modify: `src/core/CMakeLists.txt` (add sources), `tests/CMakeLists.txt` (add `test_error.cpp`, `test_encoding.cpp`, groups `error`, `encoding`), `tests/main.cpp` (add groups)
- Test: `tests/test_error.cpp`, `tests/test_encoding.cpp`

**Interfaces:**
- Produces:
  - `enum class rcedit::errc { invalid_identifier = 1, invalid_language, unknown_codec, codec_disabled, resource_not_found, ambiguous_language, read_only, self_update, corrupt_payload, empty_payload }`
  - `const std::error_category& rcedit::rcedit_category() noexcept`
  - `std::error_code rcedit::make_error_code(errc) noexcept` (ADL, `is_error_code_enum` specialized)
  - `std::error_code rcedit::LastWin32Error() noexcept`, `std::error_code rcedit::Win32Error(unsigned long) noexcept`, `std::error_code rcedit::HResultError(long hr) noexcept`
  - `rcedit::Log::{SetLevel, GetLevel, Debug, Info, Warn, Error}` with `enum class Log::Level { Debug, Info, Warn, Error }`
  - `rcedit::Out::Write(std::wstring_view)` and `template Out::Print(std::wformat_string<A...>, A&&...)` writing UTF-8 to stdout
  - `std::expected<std::string, std::error_code> rcedit::Utf16ToUtf8(std::wstring_view)`, `std::expected<std::wstring, std::error_code> rcedit::Utf8ToUtf16(std::string_view)`, `std::wstring rcedit::AnsiToUtf16(std::string_view)` (lossy, for `error_code::message()`)
  - `std::wstring rcedit::FormatError(const std::error_code&)`
  - `rcedit::ModuleHandle` (unique_ptr, `FreeLibrary`), `rcedit::FileHandle` (unique_ptr, `CloseHandle`, `INVALID_HANDLE_VALUE` aware)

- [ ] **Step 1: Write failing tests**

`tests/test_error.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include <windows.h>

#include "core/error.h"
#include "core/format.h"

namespace rcedit::test {

namespace {

void ErrcHasCategoryAndMessage()
{
    std::error_code ec = errc::codec_disabled;
    CHECK(ec.category() == rcedit_category());
    CHECK(std::string_view(ec.category().name()) == "rcedit");
    CHECK(ec.message() == "codec disabled in this build");
    CHECK(ec == errc::codec_disabled);
    CHECK(ec != errc::unknown_codec);
}

void EveryErrcHasDistinctMessage()
{
    const errc all[] = {
        errc::invalid_identifier, errc::invalid_language, errc::unknown_codec,
        errc::codec_disabled, errc::resource_not_found, errc::ambiguous_language,
        errc::read_only, errc::self_update, errc::corrupt_payload, errc::empty_payload,
    };
    for (size_t i = 0; i < std::size(all); ++i)
    {
        const std::error_code a = all[i];
        CHECK(!a.message().empty());
        CHECK(a.message() != "unknown error");
        for (size_t j = i + 1; j < std::size(all); ++j)
        {
            const std::error_code b = all[j];
            CHECK(a.message() != b.message());
        }
    }
}

void Win32ErrorUsesSystemCategory()
{
    const std::error_code ec = Win32Error(ERROR_FILE_NOT_FOUND);
    CHECK(ec.category() == std::system_category());
    CHECK(ec.value() == ERROR_FILE_NOT_FOUND);

    ::SetLastError(ERROR_ACCESS_DENIED);
    const std::error_code last = LastWin32Error();
    CHECK(last.value() == ERROR_ACCESS_DENIED);
}

void HResultErrorKeepsValue()
{
    const std::error_code ec = HResultError(E_FAIL);
    CHECK(ec.category() == std::system_category());
    CHECK(ec.value() == E_FAIL);
}

void FormatErrorShowsCategoryAndMessage()
{
    const std::wstring s = FormatError(std::error_code(errc::read_only));
    CHECK(s.find(L"rcedit") != std::wstring::npos);
    CHECK(s.find(L"read-only") != std::wstring::npos);

    const std::wstring w = FormatError(Win32Error(ERROR_FILE_NOT_FOUND));
    CHECK(w.find(L"0x2") != std::wstring::npos);
}

constexpr TestCase kCases[] = {
    {"ErrcHasCategoryAndMessage", ErrcHasCategoryAndMessage},
    {"EveryErrcHasDistinctMessage", EveryErrcHasDistinctMessage},
    {"Win32ErrorUsesSystemCategory", Win32ErrorUsesSystemCategory},
    {"HResultErrorKeepsValue", HResultErrorKeepsValue},
    {"FormatErrorShowsCategoryAndMessage", FormatErrorShowsCategoryAndMessage},
};

}  // namespace

int RunErrorTests()
{
    return RunGroup("error", kCases);
}

}  // namespace rcedit::test
```

`tests/test_encoding.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include "core/encoding.h"

namespace rcedit::test {

namespace {

void RoundTripsAscii()
{
    auto utf8 = Utf16ToUtf8(L"hello");
    CHECK(utf8.has_value());
    CHECK(*utf8 == "hello");
    auto back = Utf8ToUtf16(*utf8);
    CHECK(back.has_value());
    CHECK(*back == L"hello");
}

void RoundTripsNonAscii()
{
    const std::wstring w = L"caf\u00e9 \u65e5\u672c";
    auto utf8 = Utf16ToUtf8(w);
    CHECK(utf8.has_value());
    CHECK(utf8->size() == 4 + 1 + 1 + 6);
    auto back = Utf8ToUtf16(*utf8);
    CHECK(back.has_value());
    CHECK(*back == w);
}

void EmptyIsEmpty()
{
    auto a = Utf16ToUtf8(L"");
    CHECK(a.has_value() && a->empty());
    auto b = Utf8ToUtf16("");
    CHECK(b.has_value() && b->empty());
}

void RejectsInvalidUtf8()
{
    const char bad[] = {static_cast<char>(0xC3), static_cast<char>(0x28), 0};
    auto r = Utf8ToUtf16(bad);
    CHECK(!r.has_value());
}

void RejectsLoneSurrogate()
{
    const wchar_t bad[] = {0xD800, 0};
    auto r = Utf16ToUtf8(bad);
    CHECK(!r.has_value());
}

constexpr TestCase kCases[] = {
    {"RoundTripsAscii", RoundTripsAscii},
    {"RoundTripsNonAscii", RoundTripsNonAscii},
    {"EmptyIsEmpty", EmptyIsEmpty},
    {"RejectsInvalidUtf8", RejectsInvalidUtf8},
    {"RejectsLoneSurrogate", RejectsLoneSurrogate},
};

}  // namespace

int RunEncodingTests()
{
    return RunGroup("encoding", kCases);
}

}  // namespace rcedit::test
```

Register in `tests/main.cpp` (declare `int RunErrorTests(); int RunEncodingTests();` and add `{"error", RunErrorTests}, {"encoding", RunEncodingTests},` to `kGroups`), in `tests/CMakeLists.txt` (add the two files to `rcedit_tests` and `error encoding` to `RCEDIT_TEST_GROUPS`).

- [ ] **Step 2: Build to verify the tests fail**

```powershell
cmake --build --preset minimal-MinSizeRel
```
Expected: compile errors, `core/error.h` not found.

- [ ] **Step 3: Implement `error.h/.cpp`**

`src/core/error.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <system_error>
#include <type_traits>

namespace rcedit {

enum class errc
{
    invalid_identifier = 1,
    invalid_language,
    unknown_codec,
    codec_disabled,
    resource_not_found,
    ambiguous_language,
    read_only,
    self_update,
    corrupt_payload,
    empty_payload,
};

[[nodiscard]] const std::error_category& rcedit_category() noexcept;
[[nodiscard]] std::error_code make_error_code(errc e) noexcept;

[[nodiscard]] std::error_code Win32Error(unsigned long code) noexcept;
[[nodiscard]] std::error_code LastWin32Error() noexcept;
[[nodiscard]] std::error_code HResultError(long hr) noexcept;

}  // namespace rcedit

template <>
struct std::is_error_code_enum<rcedit::errc> : std::true_type
{
};
```

`src/core/error.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/error.h"

#include <windows.h>

namespace rcedit {

namespace {

class RceditCategory final : public std::error_category
{
public:
    const char* name() const noexcept override { return "rcedit"; }

    std::string message(int value) const override
    {
        switch (static_cast<errc>(value))
        {
            case errc::invalid_identifier:
                return "invalid resource identifier";
            case errc::invalid_language:
                return "invalid language identifier";
            case errc::unknown_codec:
                return "unknown codec";
            case errc::codec_disabled:
                return "codec disabled in this build";
            case errc::resource_not_found:
                return "resource not found";
            case errc::ambiguous_language:
                return "several languages match, specify --lang";
            case errc::read_only:
                return "engine opened read-only";
            case errc::self_update:
                return "refusing to modify the running executable";
            case errc::corrupt_payload:
                return "compressed payload is corrupt";
            case errc::empty_payload:
                return "payload is empty";
        }
        return "unknown rcedit error";
    }
};

}  // namespace

const std::error_category& rcedit_category() noexcept
{
    static const RceditCategory category;
    return category;
}

std::error_code make_error_code(errc e) noexcept
{
    return {static_cast<int>(e), rcedit_category()};
}

std::error_code Win32Error(unsigned long code) noexcept
{
    return {static_cast<int>(code), std::system_category()};
}

std::error_code LastWin32Error() noexcept
{
    return Win32Error(::GetLastError());
}

std::error_code HResultError(long hr) noexcept
{
    return {static_cast<int>(hr), std::system_category()};
}

}  // namespace rcedit
```

- [ ] **Step 4: Implement `encoding.h/.cpp`**

`src/core/encoding.h`:
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
#include <string>
#include <string_view>
#include <system_error>

namespace rcedit {

[[nodiscard]] std::expected<std::string, std::error_code> Utf16ToUtf8(std::wstring_view input);
[[nodiscard]] std::expected<std::wstring, std::error_code> Utf8ToUtf16(std::string_view input);

// Lossy conversion of an ANSI code page string (e.g. std::error_code::message()).
[[nodiscard]] std::wstring AnsiToUtf16(std::string_view input);

}  // namespace rcedit
```

`src/core/encoding.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/encoding.h"

#include <windows.h>

#include "core/error.h"

namespace rcedit {

std::expected<std::string, std::error_code> Utf16ToUtf8(std::wstring_view input)
{
    if (input.empty())
    {
        return std::string();
    }

    const int size = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return std::unexpected(LastWin32Error());
    }

    std::string out(static_cast<size_t>(size), '\0');
    const int written = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), out.data(), size, nullptr, nullptr);
    if (written != size)
    {
        return std::unexpected(LastWin32Error());
    }

    return out;
}

std::expected<std::wstring, std::error_code> Utf8ToUtf16(std::string_view input)
{
    if (input.empty())
    {
        return std::wstring();
    }

    const int size =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0)
    {
        return std::unexpected(LastWin32Error());
    }

    std::wstring out(static_cast<size_t>(size), L'\0');
    const int written = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), out.data(), size);
    if (written != size)
    {
        return std::unexpected(LastWin32Error());
    }

    return out;
}

std::wstring AnsiToUtf16(std::string_view input)
{
    if (input.empty())
    {
        return {};
    }

    const int size = ::MultiByteToWideChar(CP_ACP, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0)
    {
        return L"<unconvertible>";
    }

    std::wstring out(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, input.data(), static_cast<int>(input.size()), out.data(), size);
    return out;
}

}  // namespace rcedit
```

- [ ] **Step 5: Implement `format.h`, `log.h/.cpp`, `output.h/.cpp`, `guard.h`**

`src/core/format.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <format>
#include <string>
#include <system_error>

#include "core/encoding.h"

namespace rcedit {

// "0x2: The system cannot find the file specified." for system errors,
// "rcedit: codec disabled in this build" for rcedit errors.
[[nodiscard]] inline std::wstring FormatError(const std::error_code& ec)
{
    std::wstring message = AnsiToUtf16(ec.message());
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
    {
        message.pop_back();
    }

    if (ec.category() == std::system_category())
    {
        return std::format(L"{:#x}: {}", static_cast<unsigned long>(ec.value()), message);
    }

    return std::format(L"{}: {}", AnsiToUtf16(ec.category().name()), message);
}

}  // namespace rcedit
```

`src/core/log.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace rcedit::Log {

enum class Level
{
    Debug = 0,
    Info,
    Warn,
    Error
};

void SetLevel(Level level) noexcept;
[[nodiscard]] Level GetLevel() noexcept;

// Writes "[X] message\n" to stderr as UTF-8. Not for program output.
void Write(Level level, std::wstring_view message);

template <typename... Args>
void Debug(std::wformat_string<Args...> fmt, Args&&... args)
{
    if (GetLevel() <= Level::Debug)
    {
        Write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void Info(std::wformat_string<Args...> fmt, Args&&... args)
{
    if (GetLevel() <= Level::Info)
    {
        Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void Warn(std::wformat_string<Args...> fmt, Args&&... args)
{
    if (GetLevel() <= Level::Warn)
    {
        Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void Error(std::wformat_string<Args...> fmt, Args&&... args)
{
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace rcedit::Log
```

`src/core/log.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/log.h"

#include <cstdio>
#include <print>

#include "core/encoding.h"

namespace rcedit::Log {

namespace {

Level g_level = Level::Info;

constexpr char Tag(Level level)
{
    switch (level)
    {
        case Level::Debug:
            return 'D';
        case Level::Info:
            return 'I';
        case Level::Warn:
            return 'W';
        case Level::Error:
            return 'E';
    }
    return '?';
}

}  // namespace

void SetLevel(Level level) noexcept
{
    g_level = level;
}

Level GetLevel() noexcept
{
    return g_level;
}

void Write(Level level, std::wstring_view message)
{
    const auto utf8 = Utf16ToUtf8(message);
    std::print(stderr, "[{}] {}\n", Tag(level), utf8 ? *utf8 : std::string("<unencodable message>"));
    std::fflush(stderr);
}

}  // namespace rcedit::Log
```

`src/core/output.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace rcedit::Out {

// Program output: UTF-8 to stdout, no decoration.
void Write(std::wstring_view text);

template <typename... Args>
void Print(std::wformat_string<Args...> fmt, Args&&... args)
{
    Write(std::wstring_view(std::format(fmt, std::forward<Args>(args)...)));
}

}  // namespace rcedit::Out
```

`src/core/output.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/output.h"

#include <cstdio>

#include "core/encoding.h"

namespace rcedit::Out {

void Write(std::wstring_view text)
{
    const auto utf8 = Utf16ToUtf8(text);
    if (!utf8)
    {
        std::fputs("<unencodable output>", stdout);
        return;
    }
    std::fwrite(utf8->data(), 1, utf8->size(), stdout);
}

}  // namespace rcedit::Out
```

`src/core/guard.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <memory>

#include <windows.h>

namespace rcedit {

struct ModuleDeleter
{
    void operator()(HMODULE h) const noexcept
    {
        if (h != nullptr)
        {
            ::FreeLibrary(h);
        }
    }
};
using ModuleHandle = std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleDeleter>;

struct FileHandleDeleter
{
    void operator()(HANDLE h) const noexcept
    {
        if (h != nullptr && h != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(h);
        }
    }
};
// Holds a HANDLE from CreateFile; INVALID_HANDLE_VALUE and nullptr are both "empty".
class FileHandle
{
public:
    FileHandle() = default;
    explicit FileHandle(HANDLE h) noexcept
        : m_handle(h == INVALID_HANDLE_VALUE ? nullptr : h)
    {
    }
    [[nodiscard]] HANDLE get() const noexcept { return m_handle.get(); }
    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

private:
    std::unique_ptr<void, FileHandleDeleter> m_handle;
};

}  // namespace rcedit
```

Add to `RCEDIT_CORE_SOURCES`: `error.h error.cpp log.h log.cpp output.h output.cpp encoding.h encoding.cpp format.h guard.h`.

- [ ] **Step 6: Build and run the tests**

```powershell
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Expected: `version`, `error`, `encoding`, `imports` pass.

- [ ] **Step 7: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h tests/*.cpp tests/*.h
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add error category, logging, output, encoding and guards"
```

---

