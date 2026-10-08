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
#include <initializer_list>
#include <istream>

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

// Start of the optional header, walked from the DOS header with plain
// arithmetic rather than with the IMAGE_* structs, so the test does not
// restate the engine's own computation: e_lfanew at 0x3C, then 4 bytes of
// "PE\0\0" and 20 bytes of IMAGE_FILE_HEADER.
std::streamoff OptionalHeaderOffset( std::istream& file )
{
    file.seekg( 0x3C );
    int32_t lfanew = 0;
    file.read( reinterpret_cast< char* >( &lfanew ), sizeof( lfanew ) );
    CHECK( file.good() );
    return static_cast< std::streamoff >( lfanew ) + 4 + 20;
}

// CheckSum is 64 bytes into the optional header, the same in PE32 and PE32+.
std::streamoff CheckSumOffset( const std::filesystem::path& pe )
{
    std::ifstream file( pe, std::ios::binary );
    return OptionalHeaderOffset( file ) + 64;
}

// The certificate table is the fifth data directory, and the directories
// start at 96 in a PE32 optional header, at 112 in a PE32+ one.
std::streamoff SecurityOffset( const std::filesystem::path& pe )
{
    std::ifstream file( pe, std::ios::binary );
    const auto optional = OptionalHeaderOffset( file );
    file.seekg( optional );
    uint16_t magic = 0;
    file.read( reinterpret_cast< char* >( &magic ), sizeof( magic ) );
    CHECK( file.good() );
    return optional + ( magic == 0x20B ? 112 : 96 ) + 4 * 8;
}

std::vector< uint8_t > ReadHeaderField(
    const std::filesystem::path& pe,
    std::streamoff offset,
    size_t size )
{
    std::ifstream file( pe, std::ios::binary );
    file.seekg( offset );
    std::vector< uint8_t > bytes( size );
    file.read( reinterpret_cast< char* >( bytes.data() ), size );
    CHECK( file.good() );
    return bytes;
}

void WriteHeaderField(
    const std::filesystem::path& pe,
    std::streamoff offset,
    std::span< const uint8_t > bytes )
{
    std::ofstream file( pe, std::ios::binary | std::ios::in );
    file.seekp( offset );
    file.write(
        reinterpret_cast< const char* >( bytes.data() ),
        static_cast< std::streamsize >( bytes.size() ) );
    CHECK( file.good() );
}

std::vector< uint8_t > LittleEndian( std::initializer_list< uint32_t > words )
{
    std::vector< uint8_t > bytes;
    for( const uint32_t word : words ) {
        for( int shift = 0; shift < 32; shift += 8 ) {
            bytes.push_back( static_cast< uint8_t >( word >> shift ) );
        }
    }

    return bytes;
}

uint32_t ReadCheckSum( const std::filesystem::path& pe )
{
    const auto bytes = ReadHeaderField( pe, CheckSumOffset( pe ), 4 );
    return bytes[ 0 ] | ( bytes[ 1 ] << 8 ) | ( bytes[ 2 ] << 16 )
        | ( bytes[ 3 ] << 24 );
}

void WriteCheckSum( const std::filesystem::path& pe, uint32_t checksum )
{
    WriteHeaderField( pe, CheckSumOffset( pe ), LittleEndian( { checksum } ) );
}

// The engine only ever reads the entry, so a made-up address and size stand
// in for a real certificate table.
void WriteSecurityDirectory(
    const std::filesystem::path& pe,
    uint32_t address,
    uint32_t size )
{
    WriteHeaderField(
        pe, SecurityOffset( pe ), LittleEndian( { address, size } ) );
}

bool HasSecurityDirectory( const std::filesystem::path& pe )
{
    const auto bytes = ReadHeaderField( pe, SecurityOffset( pe ), 8 );
    return std::ranges::any_of(
        bytes, []( uint8_t byte ) { return byte != 0; } );
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

void CommitClearsCheckSum()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    WriteCheckSum( pe, 0xDEADBEEF );

    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
    CHECK_EC_OK( engine->Write( kConfig, kConfigBytes ) );
    CHECK_EC_OK( engine->Commit() );

    // The update leaves the old checksum stale; it must be zeroed, not
    // recomputed.
    CHECK( ReadCheckSum( pe ) == 0 );
    CHECK( Contains( EnumerateOf( pe ), kConfig, 9 ) );
}

void CommitOnZeroCheckSumKeepsIt()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    WriteCheckSum( pe, 0 );

    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
    CHECK_EC_OK( engine->Remove( kBaseline ) );
    CHECK_EC_OK( engine->Commit() );

    CHECK( ReadCheckSum( pe ) == 0 );
    CHECK( EnumerateOf( pe ).empty() );
}

void CommitRemovesSecurityDirectory()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    WriteSecurityDirectory( pe, 0x8290, 10064 );
    CHECK( HasSecurityDirectory( pe ) );

    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
    CHECK_EC_OK( engine->Write( kConfig, kConfigBytes ) );
    CHECK_EC_OK( engine->Commit() );

    // The update invalidated the signature, so the entry that described it
    // must be gone rather than left dangling.
    CHECK( !HasSecurityDirectory( pe ) );
    CHECK( Contains(
        EnumerateOf( pe ),
        kConfig,
        static_cast< uint32_t >( kConfigBytes.size() ) ) );
}

void DiscardLeavesHeaders()
{
    TempDir dir;
    const auto pe = CopyFixture( dir, L"a.exe" );
    WriteCheckSum( pe, 0xDEADBEEF );
    WriteSecurityDirectory( pe, 0x8290, 10064 );

    auto engine = MakeWin32Engine();
    CHECK_EC_OK( engine->Open( pe, OpenMode::ReadWrite ) );
    CHECK_EC_OK( engine->Write( kConfig, kConfigBytes ) );
    engine->Discard();

    // Nothing was written to the file, so both still describe it.
    CHECK( ReadCheckSum( pe ) == 0xDEADBEEF );
    CHECK( HasSecurityDirectory( pe ) );
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
    { "CommitClearsCheckSum", CommitClearsCheckSum },
    { "CommitOnZeroCheckSumKeepsIt", CommitOnZeroCheckSumKeepsIt },
    { "CommitRemovesSecurityDirectory", CommitRemovesSecurityDirectory },
    { "DiscardLeavesHeaders", DiscardLeavesHeaders },
};

}  // namespace

int RunEngineTests()
{
    return RunGroup( "engine", kCases );
}

}  // namespace rcedit::test
