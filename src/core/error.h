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
[[nodiscard]] std::error_code make_error_code( errc e ) noexcept;

[[nodiscard]] std::error_code Win32Error( unsigned long code ) noexcept;
[[nodiscard]] std::error_code LastWin32Error() noexcept;
[[nodiscard]] std::error_code HResultError( long hr ) noexcept;

}  // namespace rcedit

template <>
struct std::is_error_code_enum< rcedit::errc > : std::true_type
{
};
