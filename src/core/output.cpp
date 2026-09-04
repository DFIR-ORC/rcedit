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

namespace {

void WriteTo( std::FILE* stream, std::wstring_view text )
{
    const auto utf8 = Utf16ToUtf8( text );
    if( !utf8 ) {
        std::fputs( "<unencodable output>", stream );
        return;
    }

    std::fwrite( utf8->data(), 1, utf8->size(), stream );
}

}  // namespace

void Write( std::wstring_view text )
{
    WriteTo( stdout, text );
}

void WriteErr( std::wstring_view text )
{
    WriteTo( stderr, text );
    std::fflush( stderr );
}

}  // namespace rcedit::Out
