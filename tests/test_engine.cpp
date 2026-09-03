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

const ResourceKey kBaseline{ ResourceId( uint16_t( 10 ) ),
                             ResourceId( std::wstring( L"FIXTURE" ) ),
                             0 };
const ResourceKey kConfig{ ResourceId( uint16_t( 10 ) ),
                           ResourceId( std::wstring( L"CONFIG" ) ),
                           1033 };
const std::vector< uint8_t > kFixtureBytes = {
    'f', 'i', 'x', 't', 'u', 'r', 'e'
};
const std::vector< uint8_t > kConfigBytes = { '<', 'c', 'o', 'n', 'f',
                                              'i', 'g', '/', '>' };

std::vector< ResourceEntry > EnumerateOf( const std::filesystem::path& pe )
{
    auto engine = MakeWin32Engine();
    std::vector< ResourceEntry > entries;
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadOnly ) );
    CHECK_EC_OK( engine->Enumerate( entries ) );
    return entries;
}

bool Contains(
    const std::vector< ResourceEntry >& entries,
    const ResourceKey& key,
    uint32_t size )
{
    for( const auto& e : entries ) {
        if( e.key == key && e.size == size ) {
            return true;
        }
    }

    return false;
}

void OpenMissingFileFails()
{
    auto engine = MakeWin32Engine();
    const auto ec =
        engine->Open( L"C:\\does\\not\\exist.exe", OpenMode::ReadOnly );
    CHECK( static_cast< bool >( ec ) );
    CHECK( ec.category() == std::system_category() );
}

void EnumerateBaseline()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    const auto entries = EnumerateOf( pe );
    CHECK( entries.size() == 1 );
    CHECK( Contains( entries, kBaseline, 7 ) );
}

void ReadBaseline()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadOnly ) );
    std::vector< uint8_t > data;
    CHECK_EC_OK( engine->Read( kBaseline, data ) );
    CHECK( data == kFixtureBytes );
}

void ReadMissingIsNotFound()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadOnly ) );
    std::vector< uint8_t > data;
    CHECK( engine->Read( kConfig, data ) == errc::resource_not_found );

    const ResourceKey wrongLang{ kBaseline.type, kBaseline.name, 1033 };
    CHECK( engine->Read( wrongLang, data ) == errc::resource_not_found );
}

void WriteReadOnlyIsRejected()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadOnly ) );
    CHECK( engine->Write( kConfig, kConfigBytes ) == errc::read_only );
    CHECK( engine->Remove( kBaseline ) == errc::read_only );
}

void WriteWithoutLangIsRejected()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
    const ResourceKey noLang{ kConfig.type, kConfig.name, std::nullopt };
    CHECK( engine->Write( noLang, kConfigBytes ) == errc::invalid_language );
    CHECK( engine->Remove( noLang ) == errc::invalid_language );
}

void WriteEmptyIsRejected()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
    CHECK(
        engine->Write( kConfig, std::span< const uint8_t >() )
        == errc::empty_payload );
}

void WriteCommitReadBack()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
        CHECK_EC_OK( engine->Write( kConfig, kConfigBytes ) );
        CHECK_EC_OK( engine->Commit() );
        CHECK_EC_OK( engine->Commit() );  // second commit is a no-op
    }

    const auto entries = EnumerateOf( pe );
    CHECK( entries.size() == 2 );
    CHECK( Contains( entries, kBaseline, 7 ) );
    CHECK( Contains(
        entries, kConfig, static_cast< uint32_t >( kConfigBytes.size() ) ) );

    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadOnly ) );
    std::vector< uint8_t > data;
    CHECK_EC_OK( engine->Read( kConfig, data ) );
    CHECK( data == kConfigBytes );
}

void OverwriteReplacesContent()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    const std::vector< uint8_t > replacement = { 'n', 'e', 'w' };
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
        CHECK_EC_OK( engine->Write( kBaseline, replacement ) );
        CHECK_EC_OK( engine->Commit() );
    }
    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadOnly ) );
    std::vector< uint8_t > data;
    CHECK_EC_OK( engine->Read( kBaseline, data ) );
    CHECK( data == replacement );
}

void RemoveCommit()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
        CHECK_EC_OK( engine->Remove( kBaseline ) );
        CHECK_EC_OK( engine->Commit() );
    }
    const auto entries = EnumerateOf( pe );
    CHECK( entries.empty() );
}

void ReadAfterWriteIsNotPermitted()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
    std::vector< uint8_t > data;
    CHECK_EC_OK(
        engine->Read( kBaseline, data ) );  // reads work before the first write
    CHECK_EC_OK( engine->Write( kConfig, kConfigBytes ) );
    std::vector< ResourceEntry > entries;
    CHECK( engine->Enumerate( entries ) == std::errc::operation_not_permitted );
    CHECK(
        engine->Read( kBaseline, data ) == std::errc::operation_not_permitted );
    engine->Discard();
}

void DiscardLeavesFileUnchanged()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
        CHECK_EC_OK( engine->Write( kConfig, kConfigBytes ) );
        engine->Discard();
    }
    const auto entries = EnumerateOf( pe );
    CHECK( entries.size() == 1 );
    CHECK( Contains( entries, kBaseline, 7 ) );
}

void DestructorWithoutCommitDiscards()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    {
        auto engine = MakeWin32Engine();
        CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
        CHECK_EC_OK( engine->Write( kConfig, kConfigBytes ) );
        // no Commit: destructor must discard (and warn)
    }
    const auto entries = EnumerateOf( pe );
    CHECK( entries.size() == 1 );
}

constexpr TestCase kCases[] = {
    { "OpenMissingFileFails", OpenMissingFileFails },
    { "EnumerateBaseline", EnumerateBaseline },
    { "ReadBaseline", ReadBaseline },
    { "ReadMissingIsNotFound", ReadMissingIsNotFound },
    { "WriteReadOnlyIsRejected", WriteReadOnlyIsRejected },
    { "WriteWithoutLangIsRejected", WriteWithoutLangIsRejected },
    { "WriteEmptyIsRejected", WriteEmptyIsRejected },
    { "WriteCommitReadBack", WriteCommitReadBack },
    { "OverwriteReplacesContent", OverwriteReplacesContent },
    { "RemoveCommit", RemoveCommit },
    { "ReadAfterWriteIsNotPermitted", ReadAfterWriteIsNotPermitted },
    { "DiscardLeavesFileUnchanged", DiscardLeavesFileUnchanged },
    { "DestructorWithoutCommitDiscards", DestructorWithoutCommitDiscards },
};

}  // namespace

int RunEngineTests()
{
    return RunGroup( "engine", kCases );
}

}  // namespace rcedit::test
