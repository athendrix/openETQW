// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: ServerIdentityTest.cpp
Server identity (src/framework/async/ServerIdentity.h): Ed25519 against RFC 8032's test
vectors; a signed answer verifies for its own nonce, cookie, key and addresses and for
nothing else; hex round trips and refuses bad input.
===========================================================================
*/

#include "src/framework/async/ServerIdentity.h"

#include <cstdio>

static int g_failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		fprintf( stderr, "FAIL: %s\n", what );
		g_failures++;
	}
}

static void FromHex( const char* hex, uint8_t* out, int size ) {
	if ( !SQ_HexDecode( hex, out, size ) ) {
		fprintf( stderr, "bad test vector %s\n", hex );
		g_failures++;
	}
}

static void CheckVector( const char* name, const char* seedHex, const char* publicHex, const uint8_t* message, int size, const char* signatureHex ) {
	uint8_t seed[ SQ_ID_SEED_SIZE ];
	uint8_t publicKey[ SQ_ID_PUBLIC_SIZE ];
	uint8_t expected[ SQ_ID_SIGNATURE_SIZE ];
	FromHex( seedHex, seed, SQ_ID_SEED_SIZE );
	FromHex( publicHex, publicKey, SQ_ID_PUBLIC_SIZE );
	FromHex( signatureHex, expected, SQ_ID_SIGNATURE_SIZE );
	sqIdentity_t identity;
	char what[ 128 ];
	snprintf( what, sizeof( what ), "%s: key pair from the seed", name );
	Expect( SQ_IdentityFromSeed( seed, identity ) && memcmp( identity.publicKey, publicKey, SQ_ID_PUBLIC_SIZE ) == 0, what );
	uint8_t signature[ SQ_ID_SIGNATURE_SIZE ];
	snprintf( what, sizeof( what ), "%s: signature", name );
	Expect( SQ_Sign( identity, message, size, signature ) && memcmp( signature, expected, SQ_ID_SIGNATURE_SIZE ) == 0, what );
	snprintf( what, sizeof( what ), "%s: verifies", name );
	Expect( SQ_Verify( publicKey, message, size, expected ), what );
	expected[ 5 ] ^= 1;
	snprintf( what, sizeof( what ), "%s: a changed signature fails", name );
	Expect( !SQ_Verify( publicKey, message, size, expected ), what );
}

int main() {
	// RFC 8032 section 7.1, tests 1 and 2
	CheckVector( "RFC 8032 test 1",
		"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
		"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
		NULL, 0,
		"e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" );
	const uint8_t one[] = { 0x72 };
	CheckVector( "RFC 8032 test 2",
		"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
		"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
		one, 1,
		"92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" );

	// a signed answer
	uint8_t seed[ SQ_ID_SEED_SIZE ];
	for ( int i = 0; i < SQ_ID_SEED_SIZE; i++ ) {
		seed[ i ] = static_cast< uint8_t >( i * 7 + 3 );
	}
	sqIdentity_t server;
	Expect( SQ_IdentityFromSeed( seed, server ), "a key pair" );
	char id[ SQ_ID_PUBLIC_SIZE * 2 + 1 ];
	SQ_HexEncode( server.publicKey, SQ_ID_PUBLIC_SIZE, id );
	uint8_t cookie[ 16 ];
	for ( int i = 0; i < 16; i++ ) {
		cookie[ i ] = static_cast< uint8_t >( 200 - i );
	}
	const char* addresses = "203.0.113.7:27733 198.51.100.20:27733";
	char signature[ SQ_ID_SIGNATURE_SIZE * 2 + 1 ];
	Expect( SQ_SignAnswer( server, 12345, cookie, addresses, signature ), "the server signs" );
	Expect( SQ_VerifyAnswer( id, addresses, signature, 12345, cookie ), "the answer verifies" );
	Expect( !SQ_VerifyAnswer( id, addresses, signature, 12346, cookie ), "another nonce fails" );
	uint8_t otherCookie[ 16 ];
	memcpy( otherCookie, cookie, 16 );
	otherCookie[ 0 ] ^= 0x80;
	Expect( !SQ_VerifyAnswer( id, addresses, signature, 12345, otherCookie ), "another cookie fails" );
	Expect( !SQ_VerifyAnswer( id, "198.51.100.1:27733 198.51.100.20:27733", signature, 12345, cookie ), "other addresses fail" );
	Expect( !SQ_VerifyAnswer( id, "", signature, 12345, cookie ), "no addresses fails" );

	seed[ 0 ] ^= 1;
	sqIdentity_t impostor;
	SQ_IdentityFromSeed( seed, impostor );
	char impostorId[ SQ_ID_PUBLIC_SIZE * 2 + 1 ];
	SQ_HexEncode( impostor.publicKey, SQ_ID_PUBLIC_SIZE, impostorId );
	Expect( !SQ_VerifyAnswer( impostorId, addresses, signature, 12345, cookie ), "another key fails" );
	char impostorSignature[ SQ_ID_SIGNATURE_SIZE * 2 + 1 ];
	SQ_SignAnswer( impostor, 12345, cookie, addresses, impostorSignature );
	Expect( !SQ_VerifyAnswer( id, addresses, impostorSignature, 12345, cookie ), "another key's signature fails" );

	// hex
	uint8_t bytes[ 4 ];
	Expect( SQ_HexDecode( "00fFa1", bytes, 3 ) && bytes[ 0 ] == 0 && bytes[ 1 ] == 0xff && bytes[ 2 ] == 0xa1, "hex decodes either case" );
	Expect( !SQ_HexDecode( "00ff", bytes, 3 ), "hex of the wrong length" );
	Expect( !SQ_HexDecode( "00fg", bytes, 2 ), "not hex" );
	Expect( !SQ_HexDecode( NULL, bytes, 2 ), "no text" );
	Expect( !SQ_VerifyAnswer( "", addresses, signature, 12345, cookie ), "an empty id" );
	Expect( !SQ_VerifyAnswer( id, addresses, "", 12345, cookie ), "an empty signature" );

	// addresses longer than a value can hold are not signed
	char longAddresses[ SQ_ID_MAX_ADDRESSES_LENGTH + 2 ];
	memset( longAddresses, 'a', sizeof( longAddresses ) - 1 );
	longAddresses[ sizeof( longAddresses ) - 1 ] = '\0';
	Expect( !SQ_SignAnswer( server, 1, cookie, longAddresses, signature ), "too long addresses" );

	if ( g_failures == 0 ) {
		printf( "ServerIdentityTest: all checks passed\n" );
	}
	return g_failures == 0 ? 0 : 1;
}
