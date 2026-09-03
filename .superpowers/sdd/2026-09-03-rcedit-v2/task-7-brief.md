### Task 7: Resource engine interface and Win32 implementation

**Files:**
- Create: `src/core/engine.h`, `src/core/engine_win32.cpp`, `tests/fixture/fixture.c`, `tests/fixture/fixture.rc`, `tests/temp.h`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` (fixture target, pass fixture path to every group), `tests/main.cpp`
- Test: `tests/test_engine.cpp`

**Interfaces:**
- Consumes: `ResourceKey`, `ToLpcwstr`, `FromLpcwstr`, `errc`, `LastWin32Error`, `ModuleHandle`, `Log`.
- Produces:
  ```cpp
  enum class OpenMode { ReadOnly, ReadWrite };
  struct ResourceEntry { ResourceKey key; uint32_t size; };   // key.lang always set
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
  Test helpers in `tests/temp.h`: `class TempDir` (unique directory under the system temp path, removed in the destructor, `const fs::path& Path() const`), `fs::path CopyFixture(const TempDir&, std::wstring_view fileName)`.
  Fixture baseline: exactly one resource, type `RT_RCDATA` (10), name `FIXTURE`, lang `0`, content `"fixture"` (7 bytes, no terminator).

Engine rules (from the spec):
- `Open` loads the file with `LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE)` in both modes. The image is never executed.
- `Enumerate` and `Read` need the loaded module. After the first `Write`/`Remove` the module is freed and an update session is open; `Enumerate`/`Read` then return `std::errc::operation_not_permitted`. Ops always read before writing.
- `Write`/`Remove` in `ReadOnly` return `errc::read_only`; with `key.lang` unset return `errc::invalid_language`; a zero-length `Write` returns `errc::empty_payload` (Win32 treats null/zero as delete).
- `Commit` with no open session is a no-op success. `Discard` always leaves the file untouched. The destructor discards and warns if a session was left open.
- Enumeration errors `ERROR_RESOURCE_DATA_NOT_FOUND` (1812), `ERROR_RESOURCE_TYPE_NOT_FOUND` (1813), `ERROR_RESOURCE_NAME_NOT_FOUND` (1814), `ERROR_RESOURCE_LANG_NOT_FOUND` (1815) mean "nothing there", not failure. `Read` maps 1813/1814/1815 to `errc::resource_not_found`.

- [ ] **Step 1: Write the fixture and test helpers**

`tests/fixture/fixture.c`:
```c
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
// Minimal PE used by the engine and CLI tests. Its only resource comes from fixture.rc.

int main(void)
{
    return 0;
}
```

`tests/fixture/fixture.rc`:
```
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
LANGUAGE 0, 0
FIXTURE RCDATA { "fixture" }
```

`tests/temp.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <filesystem>
#include <format>
#include <random>
#include <string_view>

#include "context.h"

namespace rcedit::test {

class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        std::error_code ec;
        const auto base = std::filesystem::temp_directory_path(ec) / L"rcedit_tests";
        do
        {
            m_path = base / std::format(L"{:08x}", rd());
        } while (std::filesystem::exists(m_path));
        std::filesystem::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

inline std::filesystem::path CopyFixture(const TempDir& dir, std::wstring_view fileName)
{
    const auto target = dir.Path() / fileName;
    std::filesystem::copy_file(g_fixturePath, target, std::filesystem::copy_options::overwrite_existing);
    return target;
}

}  // namespace rcedit::test
```

In `tests/CMakeLists.txt`, before the `foreach` over groups:
```cmake
add_executable(rcedit_fixture fixture/fixture.c fixture/fixture.rc)
add_dependencies(rcedit_tests rcedit_fixture)
```
and change the loop so every group receives the fixture path:
```cmake
foreach(group IN LISTS RCEDIT_TEST_GROUPS)
    add_test(NAME ${group} COMMAND rcedit_tests ${group} $<TARGET_FILE:rcedit_fixture>)
endforeach()
```
Add `temp.h` and `test_engine.cpp` to `rcedit_tests`, and `engine` to `RCEDIT_TEST_GROUPS`.

- [ ] **Step 2: Write failing tests**

`tests/test_engine.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"
#include "temp.h"

#include "core/engine.h"
#include "core/error.h"

namespace rcedit::test {

namespace {

const ResourceKey kBaseline{ResourceId(uint16_t(10)), ResourceId(std::wstring(L"FIXTURE")), 0};
const ResourceKey kConfig{ResourceId(uint16_t(10)), ResourceId(std::wstring(L"CONFIG")), 1033};
const std::vector<uint8_t> kFixtureBytes = {'f', 'i', 'x', 't', 'u', 'r', 'e'};
const std::vector<uint8_t> kConfigBytes = {'<', 'c', 'o', 'n', 'f', 'i', 'g', '/', '>'};

std::vector<ResourceEntry> EnumerateOf(const std::filesystem::path& pe)
{
    auto engine = MakeWin32Engine();
    std::vector<ResourceEntry> entries;
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadOnly));
    CHECK_EC_OK(engine->Enumerate(entries));
    return entries;
}

bool Contains(const std::vector<ResourceEntry>& entries, const ResourceKey& key, uint32_t size)
{
    for (const auto& e : entries)
    {
        if (e.key == key && e.size == size)
        {
            return true;
        }
    }
    return false;
}

void OpenMissingFileFails()
{
    auto engine = MakeWin32Engine();
    const auto ec = engine->Open(L"C:\\does\\not\\exist.exe", OpenMode::ReadOnly);
    CHECK(static_cast<bool>(ec));
    CHECK(ec.category() == std::system_category());
}

void EnumerateBaseline()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    const auto entries = EnumerateOf(pe);
    CHECK(entries.size() == 1);
    CHECK(Contains(entries, kBaseline, 7));
}

void ReadBaseline()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadOnly));
    std::vector<uint8_t> data;
    CHECK_EC_OK(engine->Read(kBaseline, data));
    CHECK(data == kFixtureBytes);
}

void ReadMissingIsNotFound()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadOnly));
    std::vector<uint8_t> data;
    CHECK(engine->Read(kConfig, data) == errc::resource_not_found);

    const ResourceKey wrongLang{kBaseline.type, kBaseline.name, 1033};
    CHECK(engine->Read(wrongLang, data) == errc::resource_not_found);
}

void WriteReadOnlyIsRejected()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadOnly));
    CHECK(engine->Write(kConfig, kConfigBytes) == errc::read_only);
    CHECK(engine->Remove(kBaseline) == errc::read_only);
}

void WriteWithoutLangIsRejected()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
    const ResourceKey noLang{kConfig.type, kConfig.name, std::nullopt};
    CHECK(engine->Write(noLang, kConfigBytes) == errc::invalid_language);
    CHECK(engine->Remove(noLang) == errc::invalid_language);
}

void WriteEmptyIsRejected()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
    CHECK(engine->Write(kConfig, std::span<const uint8_t>()) == errc::empty_payload);
}

void WriteCommitReadBack()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
        CHECK_EC_OK(engine->Write(kConfig, kConfigBytes));
        CHECK_EC_OK(engine->Commit());
        CHECK_EC_OK(engine->Commit());  // second commit is a no-op
    }

    const auto entries = EnumerateOf(pe);
    CHECK(entries.size() == 2);
    CHECK(Contains(entries, kBaseline, 7));
    CHECK(Contains(entries, kConfig, static_cast<uint32_t>(kConfigBytes.size())));

    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadOnly));
    std::vector<uint8_t> data;
    CHECK_EC_OK(engine->Read(kConfig, data));
    CHECK(data == kConfigBytes);
}

void OverwriteReplacesContent()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    const std::vector<uint8_t> replacement = {'n', 'e', 'w'};
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
        CHECK_EC_OK(engine->Write(kBaseline, replacement));
        CHECK_EC_OK(engine->Commit());
    }
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadOnly));
    std::vector<uint8_t> data;
    CHECK_EC_OK(engine->Read(kBaseline, data));
    CHECK(data == replacement);
}

void RemoveCommit()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
        CHECK_EC_OK(engine->Remove(kBaseline));
        CHECK_EC_OK(engine->Commit());
    }
    const auto entries = EnumerateOf(pe);
    CHECK(entries.empty());
}

void ReadAfterWriteIsNotPermitted()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
    std::vector<uint8_t> data;
    CHECK_EC_OK(engine->Read(kBaseline, data));  // reads work before the first write
    CHECK_EC_OK(engine->Write(kConfig, kConfigBytes));
    std::vector<ResourceEntry> entries;
    CHECK(engine->Enumerate(entries) == std::errc::operation_not_permitted);
    CHECK(engine->Read(kBaseline, data) == std::errc::operation_not_permitted);
    engine->Discard();
}

void DiscardLeavesFileUnchanged()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
        CHECK_EC_OK(engine->Write(kConfig, kConfigBytes));
        engine->Discard();
    }
    const auto entries = EnumerateOf(pe);
    CHECK(entries.size() == 1);
    CHECK(Contains(entries, kBaseline, 7));
}

void DestructorWithoutCommitDiscards()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK(engine->Open(pe, OpenMode::ReadWrite));
        CHECK_EC_OK(engine->Write(kConfig, kConfigBytes));
        // no Commit: destructor must discard (and warn)
    }
    const auto entries = EnumerateOf(pe);
    CHECK(entries.size() == 1);
}

constexpr TestCase kCases[] = {
    {"OpenMissingFileFails", OpenMissingFileFails},
    {"EnumerateBaseline", EnumerateBaseline},
    {"ReadBaseline", ReadBaseline},
    {"ReadMissingIsNotFound", ReadMissingIsNotFound},
    {"WriteReadOnlyIsRejected", WriteReadOnlyIsRejected},
    {"WriteWithoutLangIsRejected", WriteWithoutLangIsRejected},
    {"WriteEmptyIsRejected", WriteEmptyIsRejected},
    {"WriteCommitReadBack", WriteCommitReadBack},
    {"OverwriteReplacesContent", OverwriteReplacesContent},
    {"RemoveCommit", RemoveCommit},
    {"ReadAfterWriteIsNotPermitted", ReadAfterWriteIsNotPermitted},
    {"DiscardLeavesFileUnchanged", DiscardLeavesFileUnchanged},
    {"DestructorWithoutCommitDiscards", DestructorWithoutCommitDiscards},
};

}  // namespace

int RunEngineTests()
{
    return RunGroup("engine", kCases);
}

}  // namespace rcedit::test
```

Register `RunEngineTests` in `tests/main.cpp`.

- [ ] **Step 3: Build to verify failure**

```powershell
cmake --build --preset minimal-MinSizeRel
```
Expected: `core/engine.h` not found.

- [ ] **Step 4: Implement `engine.h` and `engine_win32.cpp`**

`src/core/engine.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>
#include <vector>

#include "core/resource_id.h"

namespace rcedit {

enum class OpenMode
{
    ReadOnly,
    ReadWrite
};

struct ResourceEntry
{
    ResourceKey key;  // lang is always set
    uint32_t size;
};

// Access to the resources of one PE file. Not thread-safe.
//
// Lifecycle: Open, then any number of Enumerate/Read, then Write/Remove
// (ReadWrite only), then Commit or Discard. Once a Write/Remove was issued,
// Enumerate/Read return std::errc::operation_not_permitted until Commit or
// Discard, after which the engine must be reopened.
class ResourceEngine
{
public:
    virtual ~ResourceEngine() = default;

    [[nodiscard]] virtual std::error_code Open(const std::filesystem::path& path, OpenMode mode) = 0;
    [[nodiscard]] virtual std::error_code Enumerate(std::vector<ResourceEntry>& entries) = 0;
    [[nodiscard]] virtual std::error_code Read(const ResourceKey& key, std::vector<uint8_t>& data) = 0;

    // key.lang must be set. Empty data is errc::empty_payload.
    [[nodiscard]] virtual std::error_code Write(const ResourceKey& key, std::span<const uint8_t> data) = 0;
    [[nodiscard]] virtual std::error_code Remove(const ResourceKey& key) = 0;

    [[nodiscard]] virtual std::error_code Commit() = 0;
    virtual void Discard() = 0;
};

[[nodiscard]] std::unique_ptr<ResourceEngine> MakeWin32Engine();

}  // namespace rcedit
```

`src/core/engine_win32.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/engine.h"

#include <windows.h>

#include "core/error.h"
#include "core/format.h"
#include "core/guard.h"
#include "core/log.h"

namespace rcedit {

namespace {

bool IsNotFound(unsigned long code) noexcept
{
    return code == ERROR_RESOURCE_TYPE_NOT_FOUND || code == ERROR_RESOURCE_NAME_NOT_FOUND
        || code == ERROR_RESOURCE_LANG_NOT_FOUND;
}

bool IsEnumerationEnd(unsigned long code) noexcept
{
    return code == ERROR_RESOURCE_DATA_NOT_FOUND || IsNotFound(code) || code == ERROR_RESOURCE_ENUM_USER_STOP
        || code == ERROR_SUCCESS;
}

struct EnumContext
{
    HMODULE module = nullptr;
    std::vector<ResourceEntry>* entries = nullptr;
    std::error_code ec;
    ResourceId type;
    ResourceId name;
};

BOOL CALLBACK OnLanguage(HMODULE module, LPCWSTR type, LPCWSTR name, WORD lang, LONG_PTR param)
{
    auto* ctx = reinterpret_cast<EnumContext*>(param);

    HRSRC found = ::FindResourceExW(module, type, name, lang);
    if (found == nullptr)
    {
        ctx->ec = LastWin32Error();
        return FALSE;
    }

    const DWORD size = ::SizeofResource(module, found);
    ctx->entries->push_back(ResourceEntry{ResourceKey{ctx->type, ctx->name, lang}, size});
    return TRUE;
}

BOOL CALLBACK OnName(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    auto* ctx = reinterpret_cast<EnumContext*>(param);
    ctx->name = FromLpcwstr(name);

    if (!::EnumResourceLanguagesW(module, type, name, OnLanguage, param))
    {
        const DWORD code = ::GetLastError();
        if (!IsEnumerationEnd(code))
        {
            ctx->ec = Win32Error(code);
            return FALSE;
        }
    }
    return ctx->ec ? FALSE : TRUE;
}

BOOL CALLBACK OnType(HMODULE module, LPWSTR type, LONG_PTR param)
{
    auto* ctx = reinterpret_cast<EnumContext*>(param);
    ctx->type = FromLpcwstr(type);

    if (!::EnumResourceNamesW(module, type, OnName, param))
    {
        const DWORD code = ::GetLastError();
        if (!IsEnumerationEnd(code))
        {
            ctx->ec = Win32Error(code);
            return FALSE;
        }
    }
    return ctx->ec ? FALSE : TRUE;
}

class Win32Engine final : public ResourceEngine
{
public:
    Win32Engine() = default;
    ~Win32Engine() override
    {
        if (m_update != nullptr)
        {
            Log::Warn(L"Resource update session on '{}' was not committed, discarding", m_path.wstring());
            Discard();
        }
    }

    std::error_code Open(const std::filesystem::path& path, OpenMode mode) override
    {
        Discard();
        m_module.reset();
        m_path = path;
        m_mode = mode;

        HMODULE module = ::LoadLibraryExW(
            path.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
        if (module == nullptr)
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed LoadLibraryExW '{}' [{}]", path.wstring(), FormatError(ec));
            return ec;
        }

        m_module.reset(module);
        return {};
    }

    std::error_code Enumerate(std::vector<ResourceEntry>& entries) override
    {
        if (!m_module)
        {
            return std::make_error_code(std::errc::operation_not_permitted);
        }

        std::vector<ResourceEntry> found;
        EnumContext ctx;
        ctx.module = m_module.get();
        ctx.entries = &found;

        if (!::EnumResourceTypesW(m_module.get(), OnType, reinterpret_cast<LONG_PTR>(&ctx)))
        {
            const DWORD code = ::GetLastError();
            if (ctx.ec)
            {
                return ctx.ec;
            }
            if (!IsEnumerationEnd(code))
            {
                const auto ec = Win32Error(code);
                Log::Debug(L"Failed EnumResourceTypesW [{}]", FormatError(ec));
                return ec;
            }
        }
        if (ctx.ec)
        {
            return ctx.ec;
        }

        entries = std::move(found);
        return {};
    }

    std::error_code Read(const ResourceKey& key, std::vector<uint8_t>& data) override
    {
        if (!m_module)
        {
            return std::make_error_code(std::errc::operation_not_permitted);
        }
        if (!key.lang)
        {
            return make_error_code(errc::invalid_language);
        }

        HRSRC found = ::FindResourceExW(m_module.get(), ToLpcwstr(key.type), ToLpcwstr(key.name), *key.lang);
        if (found == nullptr)
        {
            const DWORD code = ::GetLastError();
            return IsNotFound(code) ? make_error_code(errc::resource_not_found) : Win32Error(code);
        }

        HGLOBAL loaded = ::LoadResource(m_module.get(), found);
        if (loaded == nullptr)
        {
            return LastWin32Error();
        }
        const DWORD size = ::SizeofResource(m_module.get(), found);
        const void* bytes = ::LockResource(loaded);
        if (bytes == nullptr && size != 0)
        {
            return LastWin32Error();
        }

        const auto* begin = static_cast<const uint8_t*>(bytes);
        data.assign(begin, begin + size);
        return {};
    }

    std::error_code Write(const ResourceKey& key, std::span<const uint8_t> data) override
    {
        if (data.empty())
        {
            return make_error_code(errc::empty_payload);
        }
        if (const auto ec = BeginUpdate(key))
        {
            return ec;
        }

        // UpdateResourceW takes a non-const pointer but does not modify the data.
        auto* bytes = const_cast<uint8_t*>(data.data());
        if (!::UpdateResourceW(
                m_update, ToLpcwstr(key.type), ToLpcwstr(key.name), *key.lang, bytes, static_cast<DWORD>(data.size())))
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed UpdateResourceW [{}]", FormatError(ec));
            return ec;
        }
        return {};
    }

    std::error_code Remove(const ResourceKey& key) override
    {
        if (const auto ec = BeginUpdate(key))
        {
            return ec;
        }

        if (!::UpdateResourceW(m_update, ToLpcwstr(key.type), ToLpcwstr(key.name), *key.lang, nullptr, 0))
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed UpdateResourceW (delete) [{}]", FormatError(ec));
            return ec;
        }
        return {};
    }

    std::error_code Commit() override
    {
        if (m_update == nullptr)
        {
            return {};
        }

        HANDLE handle = m_update;
        m_update = nullptr;
        if (!::EndUpdateResourceW(handle, FALSE))
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed EndUpdateResourceW [{}]", FormatError(ec));
            return ec;
        }
        return {};
    }

    void Discard() override
    {
        if (m_update == nullptr)
        {
            return;
        }
        HANDLE handle = m_update;
        m_update = nullptr;
        if (!::EndUpdateResourceW(handle, TRUE))
        {
            Log::Debug(L"Failed EndUpdateResourceW (discard) [{}]", FormatError(LastWin32Error()));
        }
    }

private:
    // Validates a write and opens the update session on first use.
    std::error_code BeginUpdate(const ResourceKey& key)
    {
        if (m_mode != OpenMode::ReadWrite)
        {
            return make_error_code(errc::read_only);
        }
        if (!key.lang)
        {
            return make_error_code(errc::invalid_language);
        }
        if (m_update != nullptr)
        {
            return {};
        }

        // The exclusive datafile mapping blocks BeginUpdateResource.
        m_module.reset();

        m_update = ::BeginUpdateResourceW(m_path.c_str(), FALSE);
        if (m_update == nullptr)
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed BeginUpdateResourceW '{}' [{}]", m_path.wstring(), FormatError(ec));
            return ec;
        }
        return {};
    }

    std::filesystem::path m_path;
    OpenMode m_mode = OpenMode::ReadOnly;
    ModuleHandle m_module;
    HANDLE m_update = nullptr;
};

}  // namespace

std::unique_ptr<ResourceEngine> MakeWin32Engine()
{
    return std::make_unique<Win32Engine>();
}

}  // namespace rcedit
```

Add `engine.h engine_win32.cpp` to `RCEDIT_CORE_SOURCES`.

- [ ] **Step 5: Build and run the tests**

```powershell
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Expected: `engine` passes. If `EnumerateBaseline` reports a language other than 0, check that `LANGUAGE 0, 0` is the first statement in `fixture.rc`. If `WriteCommitReadBack` fails at `BeginUpdateResourceW` with `0x20` (sharing violation), the module was not freed before the update; check `BeginUpdate`.

- [ ] **Step 6: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h tests/*.cpp tests/*.h
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add resource engine interface and Win32 implementation"
```

---

