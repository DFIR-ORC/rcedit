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

    std::string message( int value ) const override
    {
        switch( static_cast< errc >( value ) ) {
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

std::error_code make_error_code( errc e ) noexcept
{
    return { static_cast< int >( e ), rcedit_category() };
}

std::error_code Win32Error( unsigned long code ) noexcept
{
    return { static_cast< int >( code ), std::system_category() };
}

std::error_code LastWin32Error() noexcept
{
    return Win32Error( ::GetLastError() );
}

std::error_code HResultError( long hr ) noexcept
{
    return { static_cast< int >( hr ), std::system_category() };
}

}  // namespace rcedit
