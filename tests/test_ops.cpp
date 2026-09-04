//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"
#include "temp.h"

#include <algorithm>
#include <fstream>

#include <windows.h>

#include "core/codec.h"
#include "core/engine.h"
#include "core/error.h"
#include "core/ops.h"

namespace rcedit::test {

namespace {

const ResourceId kRcData( uint16_t( 10 ) );
const ResourceKey kBaseline{ kRcData,
                             ResourceId( std::wstring( L"FIXTURE" ) ),
                             0 };
const ResourceKey kConfigNoLang{ kRcData,
                                 ResourceId( std::wstring( L"CONFIG" ) ),
                                 std::nullopt };
const ResourceKey kConfigNeutral{ kRcData,
                                  ResourceId( std::wstring( L"CONFIG" ) ),
                                  0 };
const ResourceKey kConfigFr{ kRcData,
                             ResourceId( std::wstring( L"CONFIG" ) ),
                             1036 };
const ResourceKey kConfigEn{ kRcData,
                             ResourceId( std::wstring( L"CONFIG" ) ),
                             1033 };

#ifdef RCEDIT_HAS_ZSTD
constexpr CodecId kPreviewCodec = CodecId::Zstd;
#elif defined( RCEDIT_HAS_7Z )
constexpr CodecId kPreviewCodec = CodecId::SevenZip;
#endif

std::vector< uint8_t > Bytes( std::string_view s )
{
    return std::vector< uint8_t >( s.begin(), s.end() );
}

std::vector< uint8_t > Pattern( size_t size )
{
    std::vector< uint8_t > v( size );
    for( size_t i = 0; i < size; ++i ) {
        v[ i ] = static_cast< uint8_t >( ( i * 13 ) % 251 );
    }

    return v;
}

// Writes a file that is not a valid PE, so LoadLibraryExW (engine.Open)
// deterministically fails on it -- used to force a failure after
// CopyToOutput has already created 'output'.
std::filesystem::path WriteNotAPe(
    const TempDir& dir,
    std::wstring_view fileName )
{
    const auto path = dir.Path() / fileName;
    std::ofstream f( path, std::ios::binary );
    f << "not a PE file";
    return path;
}

std::vector< ListEntry > ListAll( const std::filesystem::path& pe )
{
    auto engine = MakeWin32Engine();
    std::vector< ListEntry > entries;
    CHECK_EC_OK( List( *engine, pe, ListOptions{}, entries ) );
    return entries;
}

const ListEntry* Find(
    const std::vector< ListEntry >& entries,
    const ResourceKey& key )
{
    for( const auto& e : entries ) {
        if( e.key == key ) {
            return &e;
        }
    }

    return nullptr;
}

void ListBaseline()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    const auto entries = ListAll( pe );
    CHECK( entries.size() == 1 );
    const auto* e = Find( entries, kBaseline );
    CHECK(
        e != nullptr && e->size == 7 && e->codec == CodecId::None
        && !e->contentSize.has_value() );
}

void ListCarriesPreview()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();

    CHECK_EC_OK(
        Set( *engine,
             pe,
             kConfigNoLang,
             Bytes( "<config>hello world</config>" ),
             CodecId::None,
             std::nullopt ) );

    const auto entries = ListAll( pe );
    const auto* e = Find( entries, kConfigNeutral );
    CHECK( e != nullptr );

    // Capped at kPreviewBytes even though the resource is longer.
    CHECK( e->preview.size() == kPreviewBytes );
    CHECK( FormatPreview( e->preview ) == L"<config>hello wo" );

    // A resource shorter than the cap previews whole.
    const auto* baseline = Find( entries, kBaseline );
    CHECK( baseline != nullptr );
    CHECK( baseline->preview.size() == 7 );
    CHECK( FormatPreview( baseline->preview ) == L"fixture" );
}

#if defined( RCEDIT_HAS_ZSTD ) || defined( RCEDIT_HAS_7Z )
// A compressed resource previews the bytes as stored, not the payload: List
// never decompresses, so what shows up is the codec's own header.
void ListPreviewIsNotDecompressed()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    const auto payload = Pattern( 50000 );

    CHECK_EC_OK( Set(
        *engine, pe, kConfigNoLang, payload, kPreviewCodec, std::nullopt ) );

    const auto entries = ListAll( pe );
    const auto* e = Find( entries, kConfigNeutral );
    CHECK( e != nullptr && e->codec == kPreviewCodec );
    CHECK( e->preview.size() == kPreviewBytes );
    CHECK(
        !std::equal( e->preview.begin(), e->preview.end(), payload.begin() ) );
}
#endif

void FormatPreviewReplacesUnprintableBytes()
{
    const std::vector< uint8_t > mixed{ 0x89, 'P',  'N',  'G',
                                        0x0D, 0x0A, 0x1A, 0x0A };
    CHECK( FormatPreview( mixed ) == L".PNG...." );

    // 0x20 and 0x7E are the edges of the printable range.
    const std::vector< uint8_t > edges{ 0x1F, 0x20, 0x7E, 0x7F };
    CHECK( FormatPreview( edges ) == L". ~." );

    CHECK( FormatPreview( {} ) == L"-" );
}

void ListFilters()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    std::vector< ListEntry > entries;

    CHECK_EC_OK(
        List( *engine, pe, ListOptions{ kRcData, std::nullopt }, entries ) );
    CHECK( entries.size() == 1 );

    CHECK_EC_OK( List(
        *engine,
        pe,
        ListOptions{ ResourceId( uint16_t( 3 ) ), std::nullopt },
        entries ) );
    CHECK( entries.empty() );

    CHECK_EC_OK( List(
        *engine,
        pe,
        ListOptions{ std::nullopt, ResourceId( std::wstring( L"FIXTURE" ) ) },
        entries ) );
    CHECK( entries.size() == 1 );

    CHECK_EC_OK( List(
        *engine,
        pe,
        ListOptions{ std::nullopt, ResourceId( std::wstring( L"NOPE" ) ) },
        entries ) );
    CHECK( entries.empty() );
}

void SetThenGetUncompressed()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    const auto payload = Bytes( "<config/>" );

    CHECK_EC_OK( Set(
        *engine, pe, kConfigNoLang, payload, CodecId::None, std::nullopt ) );

    const auto entries = ListAll( pe );
    CHECK( entries.size() == 2 );
    const auto* e = Find( entries, kConfigNeutral );
    CHECK(
        e != nullptr && e->size == payload.size()
        && e->codec == CodecId::None );

    std::vector< uint8_t > out;
    CHECK_EC_OK( Get( *engine, pe, kConfigNoLang, false, out ) );
    CHECK( out == payload );
    CHECK_EC_OK( Get( *engine, pe, kConfigNeutral, true, out ) );
    CHECK( out == payload );
}

void SetEmptyFails()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK(
        Set( *engine,
             pe,
             kConfigNoLang,
             std::span< const uint8_t >(),
             CodecId::None,
             std::nullopt )
        == errc::empty_payload );
}

void SetWithOutputLeavesSourceUnchanged()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    const auto out = dir.Path() / L"b.exe";
    auto engine = MakeWin32Engine();

    CHECK_EC_OK(
        Set( *engine, pe, kConfigNoLang, Bytes( "x" ), CodecId::None, out ) );

    CHECK( ListAll( pe ).size() == 1 );
    CHECK( ListAll( out ).size() == 2 );
}

// A failure that strikes after CopyToOutput has already created 'output'
// (here: engine.Open fails because 'pe' is not actually a valid PE, so the
// copy at 'output' is garbage too) must not leave that stray file behind.
void SetFailureAfterCopyRemovesOutput()
{
    TempDir dir;
    const auto badPe = WriteNotAPe( dir, L"bad.exe" );
    const auto out = dir.Path() / L"out.exe";
    auto engine = MakeWin32Engine();

    const auto ec =
        Set( *engine, badPe, kConfigNoLang, Bytes( "x" ), CodecId::None, out );
    CHECK( ec );
    CHECK( !std::filesystem::exists( out ) );
}

// Same as above but for Remove: CopyToOutput succeeds (pe is a valid PE),
// but ResolveLanguage then fails deterministically because CONFIG does not
// exist in the fixture's baseline.
void RemoveFailureAfterCopyRemovesOutput()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    const auto out = dir.Path() / L"out.exe";
    auto engine = MakeWin32Engine();

    CHECK(
        Remove( *engine, pe, kConfigNoLang, out ) == errc::resource_not_found );
    CHECK( !std::filesystem::exists( out ) );
}

void SetRefusesRunningExecutable()
{
    wchar_t self[ MAX_PATH ];
    ::GetModuleFileNameW( nullptr, self, MAX_PATH );
    CHECK( IsRunningExecutable( self ) );

    auto engine = MakeWin32Engine();
    CHECK(
        Set( *engine,
             self,
             kConfigNoLang,
             Bytes( "x" ),
             CodecId::None,
             std::nullopt )
        == errc::self_update );
    CHECK(
        Remove( *engine, self, kBaseline, std::nullopt ) == errc::self_update );
}

// R21: Set must check self_update before compressing (and therefore before
// codec_disabled), and must not have copied to 'output' by the time it
// fails. Only meaningful where at least one codec is disabled in this
// build; both compiled in (the 'default' preset) has no disabled codec to
// exercise, so this test does not exist there, mirroring how
// DisabledCodecIsRejectedOnSet/Get are guarded.
#if !defined( RCEDIT_HAS_ZSTD ) || !defined( RCEDIT_HAS_7Z )
void SetErrorPrecedenceSelfUpdateBeforeCodecDisabled()
{
#    ifndef RCEDIT_HAS_ZSTD
    constexpr CodecId kDisabled = CodecId::Zstd;
#    else
    constexpr CodecId kDisabled = CodecId::SevenZip;
#    endif

    wchar_t self[ MAX_PATH ];
    ::GetModuleFileNameW( nullptr, self, MAX_PATH );
    CHECK( IsRunningExecutable( self ) );

    auto engine = MakeWin32Engine();
    CHECK(
        Set( *engine,
             self,
             kConfigNoLang,
             Bytes( "x" ),
             kDisabled,
             std::nullopt )
        == errc::self_update );
}
#endif

void CompressedRoundTrip( CodecId codec )
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    const auto payload = Pattern( 50000 );

    CHECK_EC_OK(
        Set( *engine, pe, kConfigNoLang, payload, codec, std::nullopt ) );

    const auto entries = ListAll( pe );
    const auto* e = Find( entries, kConfigNeutral );
    CHECK( e != nullptr );
    CHECK( e->codec == codec );
    CHECK( e->size < payload.size() );
    CHECK( e->contentSize.has_value() && *e->contentSize == payload.size() );

    std::vector< uint8_t > raw;
    CHECK_EC_OK( Get( *engine, pe, kConfigNoLang, true, raw ) );
    CHECK( DetectCodec( raw ) == codec );
    CHECK( raw.size() == e->size );

    std::vector< uint8_t > unpacked;
    CHECK_EC_OK( Get( *engine, pe, kConfigNoLang, false, unpacked ) );
    CHECK( unpacked == payload );
}

void DisabledCodecIsRejectedOnSet( CodecId codec )
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK(
        Set( *engine, pe, kConfigNoLang, Bytes( "x" ), codec, std::nullopt )
        == errc::codec_disabled );
    CHECK( ListAll( pe ).size() == 1 );
}

void DisabledCodecIsRejectedOnGet(
    std::vector< uint8_t > magicPrefixed,
    CodecId codec )
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(
        Set( *engine,
             pe,
             kConfigNoLang,
             magicPrefixed,
             CodecId::None,
             std::nullopt ) );

    const auto entries = ListAll( pe );
    const auto* e = Find( entries, kConfigNeutral );
    CHECK( e != nullptr && e->codec == codec && !e->contentSize.has_value() );

    std::vector< uint8_t > out;
    CHECK(
        Get( *engine, pe, kConfigNoLang, false, out ) == errc::codec_disabled );
    CHECK_EC_OK( Get( *engine, pe, kConfigNoLang, true, out ) );
    CHECK( out == magicPrefixed );
}

#ifdef RCEDIT_HAS_ZSTD
void ZstdRoundTrip()
{
    CompressedRoundTrip( CodecId::Zstd );
}
#else
void ZstdDisabledOnSet()
{
    DisabledCodecIsRejectedOnSet( CodecId::Zstd );
}
void ZstdDisabledOnGet()
{
    DisabledCodecIsRejectedOnGet(
        { 0x28, 0xB5, 0x2F, 0xFD, 1, 2, 3, 4 }, CodecId::Zstd );
}
#endif

#ifdef RCEDIT_HAS_7Z
void SevenZipRoundTrip()
{
    CompressedRoundTrip( CodecId::SevenZip );
}
#else
void SevenZipDisabledOnSet()
{
    DisabledCodecIsRejectedOnSet( CodecId::SevenZip );
}
void SevenZipDisabledOnGet()
{
    DisabledCodecIsRejectedOnGet(
        { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, 1, 2 }, CodecId::SevenZip );
}
#endif

void LanguageResolution()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    std::vector< uint8_t > out;

    // No CONFIG at all.
    CHECK(
        Get( *engine, pe, kConfigNoLang, false, out )
        == errc::resource_not_found );

    // One language: resolves to it.
    CHECK_EC_OK( Set(
        *engine, pe, kConfigFr, Bytes( "fr" ), CodecId::None, std::nullopt ) );
    CHECK_EC_OK( Get( *engine, pe, kConfigNoLang, false, out ) );
    CHECK( out == Bytes( "fr" ) );

    // Two non-neutral languages: ambiguous, candidates listed.
    CHECK_EC_OK( Set(
        *engine, pe, kConfigEn, Bytes( "en" ), CodecId::None, std::nullopt ) );
    CHECK(
        Get( *engine, pe, kConfigNoLang, false, out )
        == errc::ambiguous_language );
    {
        auto ro = MakeWin32Engine();
        CHECK_EC_OK( ro->Open( pe, OpenMode::ReadOnly ) );
        ResourceKey resolved;
        std::vector< uint16_t > candidates;
        CHECK(
            ResolveLanguage( *ro, kConfigNoLang, resolved, candidates )
            == errc::ambiguous_language );
        CHECK( candidates.size() == 2 );
    }

    // Neutral present: wins.
    CHECK_EC_OK(
        Set( *engine,
             pe,
             kConfigNoLang,
             Bytes( "neutral" ),
             CodecId::None,
             std::nullopt ) );
    CHECK_EC_OK( Get( *engine, pe, kConfigNoLang, false, out ) );
    CHECK( out == Bytes( "neutral" ) );

    // Explicit language always wins.
    CHECK_EC_OK( Get( *engine, pe, kConfigEn, false, out ) );
    CHECK( out == Bytes( "en" ) );
}

void RemoveResolvesLanguage()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();

    CHECK_EC_OK( Set(
        *engine, pe, kConfigFr, Bytes( "fr" ), CodecId::None, std::nullopt ) );
    CHECK( ListAll( pe ).size() == 2 );

    CHECK_EC_OK( Remove( *engine, pe, kConfigNoLang, std::nullopt ) );
    CHECK( ListAll( pe ).size() == 1 );

    CHECK(
        Remove( *engine, pe, kConfigNoLang, std::nullopt )
        == errc::resource_not_found );
}

void RemoveWithOutput()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    const auto out = dir.Path() / L"b.exe";
    auto engine = MakeWin32Engine();

    CHECK_EC_OK( Remove( *engine, pe, kBaseline, out ) );
    CHECK( ListAll( pe ).size() == 1 );
    CHECK( ListAll( out ).empty() );
}

void HexdumpFormatting()
{
    const auto data = Bytes( "fixture" );
    const std::wstring line = FormatHexdump( data, std::nullopt );
    CHECK(line == L"00000000  66 69 78 74 75 72 65                              |fixture|\n");

    CHECK(
        FormatHexdump( std::span< const uint8_t >(), std::nullopt )
        == L"<empty>\n" );

    std::vector< uint8_t > twenty( 20, 0x41 );
    twenty[ 7 ] = 0x00;
    const std::wstring two = FormatHexdump( twenty, std::nullopt );
    CHECK( two.starts_with(
        L"00000000  41 41 41 41 41 41 41 00  41 41 41 41 41 41 41 41  "
        L"|AAAAAAA.AAAAAAAA|\n" ) );
    CHECK( two.find( L"00000010  41 41 41 41" ) != std::wstring::npos );

    const std::wstring limited = FormatHexdump( twenty, 4 );
    CHECK( limited.starts_with( L"00000000  41 41 41 41" ) );
    CHECK( limited.find( L"... (16 more bytes)" ) != std::wstring::npos );
}

void HexdumpOfResource()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    std::wstring text;
    CHECK_EC_OK( Hexdump( *engine, pe, kBaseline, false, std::nullopt, text ) );
    CHECK( text.starts_with( L"00000000  66 69 78 74 75 72 65" ) );
}

constexpr TestCase kCases[] = {
    { "ListBaseline", ListBaseline },
    { "ListFilters", ListFilters },
    { "ListCarriesPreview", ListCarriesPreview },
#if defined( RCEDIT_HAS_ZSTD ) || defined( RCEDIT_HAS_7Z )
    { "ListPreviewIsNotDecompressed", ListPreviewIsNotDecompressed },
#endif
    { "FormatPreviewReplacesUnprintableBytes",
      FormatPreviewReplacesUnprintableBytes },
    { "SetThenGetUncompressed", SetThenGetUncompressed },
    { "SetEmptyFails", SetEmptyFails },
    { "SetWithOutputLeavesSourceUnchanged",
      SetWithOutputLeavesSourceUnchanged },
    { "SetFailureAfterCopyRemovesOutput", SetFailureAfterCopyRemovesOutput },
    { "RemoveFailureAfterCopyRemovesOutput",
      RemoveFailureAfterCopyRemovesOutput },
    { "SetRefusesRunningExecutable", SetRefusesRunningExecutable },
#if !defined( RCEDIT_HAS_ZSTD ) || !defined( RCEDIT_HAS_7Z )
    { "SetErrorPrecedenceSelfUpdateBeforeCodecDisabled",
      SetErrorPrecedenceSelfUpdateBeforeCodecDisabled },
#endif
#ifdef RCEDIT_HAS_ZSTD
    { "ZstdRoundTrip", ZstdRoundTrip },
#else
    { "ZstdDisabledOnSet", ZstdDisabledOnSet },
    { "ZstdDisabledOnGet", ZstdDisabledOnGet },
#endif
#ifdef RCEDIT_HAS_7Z
    { "SevenZipRoundTrip", SevenZipRoundTrip },
#else
    { "SevenZipDisabledOnSet", SevenZipDisabledOnSet },
    { "SevenZipDisabledOnGet", SevenZipDisabledOnGet },
#endif
    { "LanguageResolution", LanguageResolution },
    { "RemoveResolvesLanguage", RemoveResolvesLanguage },
    { "RemoveWithOutput", RemoveWithOutput },
    { "HexdumpFormatting", HexdumpFormatting },
    { "HexdumpOfResource", HexdumpOfResource },
};

}  // namespace

int RunOpsTests()
{
    return RunGroup( "ops", kCases );
}

}  // namespace rcedit::test
