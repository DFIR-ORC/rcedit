//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/resource_id.h"

#include <array>
#include <format>

#include <windows.h>

#include "core/encoding.h"
#include "core/error.h"

namespace rcedit {

namespace {

struct Alias
{
    std::wstring_view name;  // without RT_
    uint16_t id;
};

// clang-format off
constexpr std::array<Alias, 21> kAliases = {{
    {L"CURSOR", 1},       {L"BITMAP", 2},       {L"ICON", 3},          {L"MENU", 4},
    {L"DIALOG", 5},       {L"STRING", 6},       {L"FONTDIR", 7},       {L"FONT", 8},
    {L"ACCELERATOR", 9},  {L"RCDATA", 10},      {L"MESSAGETABLE", 11}, {L"GROUP_CURSOR", 12},
    {L"GROUP_ICON", 14},  {L"VERSION", 16},     {L"DLGINCLUDE", 17},   {L"PLUGPLAY", 19},
    {L"VXD", 20},         {L"ANICURSOR", 21},   {L"ANIICON", 22},      {L"HTML", 23},
    {L"MANIFEST", 24},
}};
// clang-format on

std::optional< uint16_t > ParseUnsigned( std::wstring_view digits, int base )
{
    if( digits.empty() ) {
        return std::nullopt;
    }

    uint32_t value = 0;
    for( wchar_t c : digits ) {
        int digit = -1;
        if( c >= L'0' && c <= L'9' ) {
            digit = c - L'0';
        }
        else if( base == 16 && c >= L'a' && c <= L'f' ) {
            digit = 10 + ( c - L'a' );
        }
        else if( base == 16 && c >= L'A' && c <= L'F' ) {
            digit = 10 + ( c - L'A' );
        }
        if( digit < 0 || digit >= base ) {
            return std::nullopt;
        }
        value = value * static_cast< uint32_t >( base )
            + static_cast< uint32_t >( digit );
        if( value > 0xFFFF ) {
            return std::nullopt;
        }
    }
    return static_cast< uint16_t >( value );
}

std::optional< uint16_t > AliasToId( std::wstring_view text )
{
    if( text.size() > 3 && EqualsIgnoreCase( text.substr( 0, 3 ), L"RT_" ) ) {
        text.remove_prefix( 3 );
    }
    for( const auto& alias : kAliases ) {
        if( EqualsIgnoreCase( alias.name, text ) ) {
            return alias.id;
        }
    }
    return std::nullopt;
}

std::optional< std::wstring_view > IdToAlias( uint16_t id )
{
    for( const auto& alias : kAliases ) {
        if( alias.id == id ) {
            return alias.name;
        }
    }
    return std::nullopt;
}

}  // namespace

std::expected< ResourceId, std::error_code > ParseResourceName(
    std::wstring_view text )
{
    if( text.empty() ) {
        return std::unexpected( make_error_code( errc::invalid_identifier ) );
    }

    if( text.front() == L'#' ) {
        const auto id = ParseUnsigned( text.substr( 1 ), 10 );
        if( !id || *id == 0 ) {
            return std::unexpected(
                make_error_code( errc::invalid_identifier ) );
        }
        return ResourceId( *id );
    }

    return ResourceId( std::wstring( text ) );
}

std::expected< ResourceId, std::error_code > ParseResourceType(
    std::wstring_view text )
{
    if( const auto id = AliasToId( text ) ) {
        return ResourceId( *id );
    }
    return ParseResourceName( text );
}

std::expected< uint16_t, std::error_code > ParseLang( std::wstring_view text )
{
    std::optional< uint16_t > value;
    if( text.size() > 2 && text[ 0 ] == L'0'
        && ( text[ 1 ] == L'x' || text[ 1 ] == L'X' ) ) {
        value = ParseUnsigned( text.substr( 2 ), 16 );
    }
    else {
        value = ParseUnsigned( text, 10 );
    }

    if( !value ) {
        return std::unexpected( make_error_code( errc::invalid_language ) );
    }
    return *value;
}

std::wstring FormatResourceName( const ResourceId& id )
{
    if( const auto* n = std::get_if< uint16_t >( &id ) ) {
        return std::format( L"#{}", *n );
    }
    return std::get< std::wstring >( id );
}

std::wstring FormatResourceType( const ResourceId& id )
{
    if( const auto* n = std::get_if< uint16_t >( &id ) ) {
        if( const auto alias = IdToAlias( *n ) ) {
            return std::format( L"RT_{}", *alias );
        }
        return std::format( L"#{}", *n );
    }
    return std::get< std::wstring >( id );
}

const wchar_t* ToLpcwstr( const ResourceId& id ) noexcept
{
    if( const auto* n = std::get_if< uint16_t >( &id ) ) {
        return MAKEINTRESOURCEW( *n );
    }
    return std::get< std::wstring >( id ).c_str();
}

ResourceId FromLpcwstr( const wchar_t* value )
{
    if( IS_INTRESOURCE( value ) ) {
        return ResourceId(
            static_cast< uint16_t >(
                reinterpret_cast< uintptr_t >( value ) & 0xFFFF ) );
    }
    return ResourceId( std::wstring( value ) );
}

}  // namespace rcedit
