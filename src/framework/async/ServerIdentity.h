// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __ASYNC_SERVERIDENTITY_H__
#define __ASYNC_SERVERIDENTITY_H__

/*
A server's identity: an Ed25519 key pair the server makes once and keeps in its save path
(serverkey.dat, idAsyncServer::LoadIdentity), so the browser can tell a server it knows
from any other at whatever address it is found: on the LAN, at its public address, or at
a new address after it moved.

Every info answer (ServerQuery.h, step 4) carries the public key as the key si_id (64 hex
digits) and the addresses the server says it can be reached at as si_addrs (host:port,
separated by spaces: net_serverPublicAddress and the server's private LAN addresses). A
client asking for proof (a 1 byte after step 3's work) also gets si_idsig, an Ed25519
signature of SQ_IDENTITY_CONTEXT, the request's nonce and cookie, the public key and
si_addrs. The nonce is the client's and the cookie the server's, so a signature answers
one question only and cannot be replayed to another client.

A relay can still pass the real server's signed answer on from its own address. So the
browser takes an address as the server's only when the signed si_addrs lists it, or it
is a LAN address (SDNetBrowser.cpp). Signing costs about a millisecond and a half, so a
server signs at most SQ_IDENTITY_SIGNS_PER_SECOND answers a second and answers the rest
unsigned; only the browser's favorites and history ask for proof.

Ed25519 is TweetNaCl's (src/libs/tweetnacl). Plain C++ over it, so the asset-free unit
suite can check it against RFC 8032's test vectors (tests/ServerIdentityTest.cpp).
*/

#include <cstdint>
#include <cstring>

extern "C" {
int crypto_sign_ed25519_tweet( unsigned char* sm, unsigned long long* smlen, const unsigned char* m, unsigned long long n, const unsigned char* sk );
int crypto_sign_ed25519_tweet_open( unsigned char* m, unsigned long long* mlen, const unsigned char* sm, unsigned long long n, const unsigned char* pk );
int crypto_sign_ed25519_tweet_keypair( unsigned char* pk, unsigned char* sk );
}

// the 32 bytes TweetNaCl's next randombytes call returns, for a key pair from a saved
// seed (ServerIdentity.cpp); NULL goes back to the OS's random source
void SQ_SetKeySeed( const uint8_t* seed );

static const char SQ_IDENTITY_CONTEXT[]	= "openETQW server identity 1";
static const char SQ_KEY_ID[]			= "si_id";
static const char SQ_KEY_ADDRESSES[]	= "si_addrs";
static const char SQ_KEY_SIGNATURE[]	= "si_idsig";

const int SQ_ID_SEED_SIZE				= 32;
const int SQ_ID_PUBLIC_SIZE				= 32;
const int SQ_ID_SECRET_SIZE				= 64;
const int SQ_ID_SIGNATURE_SIZE			= 64;
const int SQ_ID_MAX_ADDRESSES_LENGTH	= 255;	// within SQ_MAX_VALUE_LENGTH
const int SQ_IDENTITY_SIGNS_PER_SECOND	= 20;

struct sqIdentity_t {
	uint8_t	publicKey[ SQ_ID_PUBLIC_SIZE ];
	uint8_t	secretKey[ SQ_ID_SECRET_SIZE ];
	bool	valid;
};

// the key pair of a 32-byte seed
inline bool SQ_IdentityFromSeed( const uint8_t seed[ SQ_ID_SEED_SIZE ], sqIdentity_t& identity ) {
	memset( &identity, 0, sizeof( identity ) );
	SQ_SetKeySeed( seed );
	const int result = crypto_sign_ed25519_tweet_keypair( identity.publicKey, identity.secretKey );
	SQ_SetKeySeed( NULL );
	identity.valid = result == 0;
	return identity.valid;
}

// lower-case hex of size bytes into out (2 * size + 1 chars)
inline void SQ_HexEncode( const uint8_t* data, int size, char* out ) {
	static const char digits[] = "0123456789abcdef";
	for ( int i = 0; i < size; i++ ) {
		out[ i * 2 ] = digits[ data[ i ] >> 4 ];
		out[ i * 2 + 1 ] = digits[ data[ i ] & 15 ];
	}
	out[ size * 2 ] = '\0';
}

// exactly 2 * size hex digits (either case) into size bytes
inline bool SQ_HexDecode( const char* text, uint8_t* out, int size ) {
	if ( text == NULL || static_cast< int >( strlen( text ) ) != size * 2 ) {
		return false;
	}
	for ( int i = 0; i < size * 2; i++ ) {
		const char c = text[ i ];
		int value;
		if ( c >= '0' && c <= '9' ) {
			value = c - '0';
		} else if ( c >= 'a' && c <= 'f' ) {
			value = c - 'a' + 10;
		} else if ( c >= 'A' && c <= 'F' ) {
			value = c - 'A' + 10;
		} else {
			return false;
		}
		if ( ( i & 1 ) == 0 ) {
			out[ i / 2 ] = static_cast< uint8_t >( value << 4 );
		} else {
			out[ i / 2 ] |= static_cast< uint8_t >( value );
		}
	}
	return true;
}

const int SQ_ID_MESSAGE_SIZE = static_cast< int >( sizeof( SQ_IDENTITY_CONTEXT ) ) + 4 + 16 + SQ_ID_PUBLIC_SIZE + SQ_ID_MAX_ADDRESSES_LENGTH;

// what a signature covers; returns its length, or -1 when the addresses are too long
inline int SQ_IdentityMessage( int nonce, const uint8_t cookie[ 16 ], const uint8_t publicKey[ SQ_ID_PUBLIC_SIZE ], const char* addresses, uint8_t out[ SQ_ID_MESSAGE_SIZE ] ) {
	const int addressesLength = addresses != NULL ? static_cast< int >( strlen( addresses ) ) : 0;
	if ( addressesLength > SQ_ID_MAX_ADDRESSES_LENGTH ) {
		return -1;
	}
	int size = 0;
	memcpy( out, SQ_IDENTITY_CONTEXT, sizeof( SQ_IDENTITY_CONTEXT ) );	// with its terminating zero
	size += sizeof( SQ_IDENTITY_CONTEXT );
	const uint32_t n = static_cast< uint32_t >( nonce );
	out[ size++ ] = static_cast< uint8_t >( n >> 24 );
	out[ size++ ] = static_cast< uint8_t >( n >> 16 );
	out[ size++ ] = static_cast< uint8_t >( n >> 8 );
	out[ size++ ] = static_cast< uint8_t >( n );
	memcpy( out + size, cookie, 16 );
	size += 16;
	memcpy( out + size, publicKey, SQ_ID_PUBLIC_SIZE );
	size += SQ_ID_PUBLIC_SIZE;
	if ( addressesLength > 0 ) {
		memcpy( out + size, addresses, addressesLength );
		size += addressesLength;
	}
	return size;
}

// a detached Ed25519 signature of a message
inline bool SQ_Sign( const sqIdentity_t& identity, const uint8_t* message, int size, uint8_t signature[ SQ_ID_SIGNATURE_SIZE ] ) {
	if ( !identity.valid || size < 0 || size > SQ_ID_MESSAGE_SIZE ) {
		return false;
	}
	uint8_t signedMessage[ SQ_ID_SIGNATURE_SIZE + SQ_ID_MESSAGE_SIZE ];
	unsigned long long signedSize = 0;
	if ( crypto_sign_ed25519_tweet( signedMessage, &signedSize, message, static_cast< unsigned long long >( size ), identity.secretKey ) != 0 ) {
		return false;
	}
	memcpy( signature, signedMessage, SQ_ID_SIGNATURE_SIZE );
	return true;
}

inline bool SQ_Verify( const uint8_t publicKey[ SQ_ID_PUBLIC_SIZE ], const uint8_t* message, int size, const uint8_t signature[ SQ_ID_SIGNATURE_SIZE ] ) {
	if ( size < 0 || size > SQ_ID_MESSAGE_SIZE ) {
		return false;
	}
	uint8_t signedMessage[ SQ_ID_SIGNATURE_SIZE + SQ_ID_MESSAGE_SIZE ];
	uint8_t opened[ SQ_ID_SIGNATURE_SIZE + SQ_ID_MESSAGE_SIZE ];
	memcpy( signedMessage, signature, SQ_ID_SIGNATURE_SIZE );
	if ( size > 0 ) {
		memcpy( signedMessage + SQ_ID_SIGNATURE_SIZE, message, size );
	}
	unsigned long long openedSize = 0;
	return crypto_sign_ed25519_tweet_open( opened, &openedSize, signedMessage, static_cast< unsigned long long >( SQ_ID_SIGNATURE_SIZE + size ), publicKey ) == 0 &&
		openedSize == static_cast< unsigned long long >( size );
}

// the server's side: the signature (hex) of an answer to nonce and cookie
inline bool SQ_SignAnswer( const sqIdentity_t& identity, int nonce, const uint8_t cookie[ 16 ], const char* addresses, char signatureHex[ SQ_ID_SIGNATURE_SIZE * 2 + 1 ] ) {
	uint8_t message[ SQ_ID_MESSAGE_SIZE ];
	const int size = SQ_IdentityMessage( nonce, cookie, identity.publicKey, addresses, message );
	uint8_t signature[ SQ_ID_SIGNATURE_SIZE ];
	if ( size < 0 || !SQ_Sign( identity, message, size, signature ) ) {
		return false;
	}
	SQ_HexEncode( signature, SQ_ID_SIGNATURE_SIZE, signatureHex );
	return true;
}

// the client's side: do si_id, si_addrs and si_idsig answer this question
inline bool SQ_VerifyAnswer( const char* idHex, const char* addresses, const char* signatureHex, int nonce, const uint8_t cookie[ 16 ] ) {
	uint8_t publicKey[ SQ_ID_PUBLIC_SIZE ];
	uint8_t signature[ SQ_ID_SIGNATURE_SIZE ];
	if ( !SQ_HexDecode( idHex, publicKey, SQ_ID_PUBLIC_SIZE ) || !SQ_HexDecode( signatureHex, signature, SQ_ID_SIGNATURE_SIZE ) ) {
		return false;
	}
	uint8_t message[ SQ_ID_MESSAGE_SIZE ];
	const int size = SQ_IdentityMessage( nonce, cookie, publicKey, addresses, message );
	return size >= 0 && SQ_Verify( publicKey, message, size, signature );
}

#endif /* !__ASYNC_SERVERIDENTITY_H__ */
