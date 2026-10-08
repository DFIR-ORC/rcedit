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
        const auto base =
            std::filesystem::temp_directory_path( ec ) / L"rcedit_tests";
        do {
            m_path = base / std::format( L"{:08x}", rd() );
        } while( std::filesystem::exists( m_path ) );

        std::filesystem::create_directories( m_path );
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all( m_path, ec );
    }

    TempDir( const TempDir& ) = delete;
    TempDir& operator=( const TempDir& ) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

inline std::filesystem::path CopyFixture(
    const TempDir& dir,
    std::wstring_view fileName )
{
    const auto target = dir.Path() / fileName;
    std::filesystem::copy_file(
        g_fixturePath,
        target,
        std::filesystem::copy_options::overwrite_existing );
    return target;
}

}  // namespace rcedit::test
