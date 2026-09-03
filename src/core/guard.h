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
    void operator()( HMODULE h ) const noexcept
    {
        if( h != nullptr ) {
            ::FreeLibrary( h );
        }
    }
};
using ModuleHandle =
    std::unique_ptr< std::remove_pointer_t< HMODULE >, ModuleDeleter >;

struct FileHandleDeleter
{
    void operator()( HANDLE h ) const noexcept
    {
        if( h != nullptr && h != INVALID_HANDLE_VALUE ) {
            ::CloseHandle( h );
        }
    }
};
// Holds a HANDLE from CreateFile; INVALID_HANDLE_VALUE and nullptr are both
// "empty".
class FileHandle
{
public:
    FileHandle() = default;
    explicit FileHandle( HANDLE h ) noexcept
        : m_handle( h == INVALID_HANDLE_VALUE ? nullptr : h )
    {
    }
    [[nodiscard]] HANDLE get() const noexcept { return m_handle.get(); }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_handle != nullptr;
    }

private:
    std::unique_ptr< void, FileHandleDeleter > m_handle;
};

}  // namespace rcedit
