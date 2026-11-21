// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __ASYNC_SHA256_H__
#define __ASYNC_SHA256_H__

/*
SHA-256 (FIPS 180-4) and HMAC-SHA-256 (RFC 2104), for the server query's stateless
cookies and proof of work (ServerQuery.h). Plain C++, no engine headers, so the
asset-free unit suite can check it against the standard test vectors
(tests/ServerQueryTest.cpp).
*/

#include <cstddef>
#include <cstdint>
#include <cstring>

class sqSha256 {
public:
	static const int DIGEST_SIZE = 32;
	static const int BLOCK_SIZE = 64;

	sqSha256() { Reset(); }

	void Reset() {
		static const uint32_t init[ 8 ] = {
			0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
		};
		memcpy( state, init, sizeof( state ) );
		length = 0;
		used = 0;
	}

	void Update( const void* data, size_t size ) {
		const uint8_t* bytes = static_cast< const uint8_t* >( data );
		length += static_cast< uint64_t >( size );
		while ( size > 0 ) {
			const size_t take = size < static_cast< size_t >( BLOCK_SIZE - used ) ? size : static_cast< size_t >( BLOCK_SIZE - used );
			memcpy( block + used, bytes, take );
			used += static_cast< int >( take );
			bytes += take;
			size -= take;
			if ( used == BLOCK_SIZE ) {
				Transform( block );
				used = 0;
			}
		}
	}

	void Final( uint8_t digest[ DIGEST_SIZE ] ) {
		const uint64_t bits = length * 8;
		const uint8_t pad = 0x80;
		Update( &pad, 1 );
		const uint8_t zero = 0;
		while ( used != BLOCK_SIZE - 8 ) {
			Update( &zero, 1 );
		}
		uint8_t lengthBytes[ 8 ];
		for ( int i = 0; i < 8; i++ ) {
			lengthBytes[ i ] = static_cast< uint8_t >( bits >> ( 56 - 8 * i ) );
		}
		Update( lengthBytes, 8 );
		for ( int i = 0; i < 8; i++ ) {
			digest[ i * 4 + 0 ] = static_cast< uint8_t >( state[ i ] >> 24 );
			digest[ i * 4 + 1 ] = static_cast< uint8_t >( state[ i ] >> 16 );
			digest[ i * 4 + 2 ] = static_cast< uint8_t >( state[ i ] >> 8 );
			digest[ i * 4 + 3 ] = static_cast< uint8_t >( state[ i ] );
		}
	}

	static void Hash( const void* data, size_t size, uint8_t digest[ DIGEST_SIZE ] ) {
		sqSha256 sha;
		sha.Update( data, size );
		sha.Final( digest );
	}

	// the digest of one already padded 64-byte block (a message of at most 55 bytes)
	static void HashPaddedBlock( const uint8_t block[ BLOCK_SIZE ], uint8_t digest[ DIGEST_SIZE ] ) {
		sqSha256 sha;
		sha.Transform( block );
		for ( int i = 0; i < 8; i++ ) {
			digest[ i * 4 + 0 ] = static_cast< uint8_t >( sha.state[ i ] >> 24 );
			digest[ i * 4 + 1 ] = static_cast< uint8_t >( sha.state[ i ] >> 16 );
			digest[ i * 4 + 2 ] = static_cast< uint8_t >( sha.state[ i ] >> 8 );
			digest[ i * 4 + 3 ] = static_cast< uint8_t >( sha.state[ i ] );
		}
	}

	// pads a message of at most 55 bytes into one block
	static void PadBlock( const void* data, int size, uint8_t block[ BLOCK_SIZE ] ) {
		memset( block, 0, BLOCK_SIZE );
		memcpy( block, data, size );
		block[ size ] = 0x80;
		const uint64_t bits = static_cast< uint64_t >( size ) * 8;
		for ( int i = 0; i < 8; i++ ) {
			block[ BLOCK_SIZE - 1 - i ] = static_cast< uint8_t >( bits >> ( 8 * i ) );
		}
	}

private:
	static uint32_t Rotr( uint32_t x, int n ) { return ( x >> n ) | ( x << ( 32 - n ) ); }

	void Transform( const uint8_t data[ BLOCK_SIZE ] ) {
		static const uint32_t k[ 64 ] = {
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
		};
		uint32_t w[ 64 ];
		for ( int i = 0; i < 16; i++ ) {
			w[ i ] = ( static_cast< uint32_t >( data[ i * 4 ] ) << 24 ) | ( static_cast< uint32_t >( data[ i * 4 + 1 ] ) << 16 ) |
				( static_cast< uint32_t >( data[ i * 4 + 2 ] ) << 8 ) | static_cast< uint32_t >( data[ i * 4 + 3 ] );
		}
		for ( int i = 16; i < 64; i++ ) {
			const uint32_t s0 = Rotr( w[ i - 15 ], 7 ) ^ Rotr( w[ i - 15 ], 18 ) ^ ( w[ i - 15 ] >> 3 );
			const uint32_t s1 = Rotr( w[ i - 2 ], 17 ) ^ Rotr( w[ i - 2 ], 19 ) ^ ( w[ i - 2 ] >> 10 );
			w[ i ] = w[ i - 16 ] + s0 + w[ i - 7 ] + s1;
		}
		uint32_t a = state[ 0 ], b = state[ 1 ], c = state[ 2 ], d = state[ 3 ];
		uint32_t e = state[ 4 ], f = state[ 5 ], g = state[ 6 ], h = state[ 7 ];
		for ( int i = 0; i < 64; i++ ) {
			const uint32_t s1 = Rotr( e, 6 ) ^ Rotr( e, 11 ) ^ Rotr( e, 25 );
			const uint32_t ch = ( e & f ) ^ ( ~e & g );
			const uint32_t t1 = h + s1 + ch + k[ i ] + w[ i ];
			const uint32_t s0 = Rotr( a, 2 ) ^ Rotr( a, 13 ) ^ Rotr( a, 22 );
			const uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
			const uint32_t t2 = s0 + maj;
			h = g; g = f; f = e; e = d + t1;
			d = c; c = b; b = a; a = t1 + t2;
		}
		state[ 0 ] += a; state[ 1 ] += b; state[ 2 ] += c; state[ 3 ] += d;
		state[ 4 ] += e; state[ 5 ] += f; state[ 6 ] += g; state[ 7 ] += h;
	}

	uint32_t	state[ 8 ];
	uint64_t	length;
	uint8_t		block[ BLOCK_SIZE ];
	int			used;
};

// HMAC-SHA-256 of message with key
inline void SQ_HmacSha256( const void* key, size_t keySize, const void* message, size_t messageSize, uint8_t mac[ sqSha256::DIGEST_SIZE ] ) {
	uint8_t keyBlock[ sqSha256::BLOCK_SIZE ];
	memset( keyBlock, 0, sizeof( keyBlock ) );
	if ( keySize > sqSha256::BLOCK_SIZE ) {
		sqSha256::Hash( key, keySize, keyBlock );
	} else {
		memcpy( keyBlock, key, keySize );
	}
	uint8_t pad[ sqSha256::BLOCK_SIZE ];
	uint8_t inner[ sqSha256::DIGEST_SIZE ];
	for ( int i = 0; i < sqSha256::BLOCK_SIZE; i++ ) {
		pad[ i ] = keyBlock[ i ] ^ 0x36;
	}
	sqSha256 sha;
	sha.Update( pad, sizeof( pad ) );
	sha.Update( message, messageSize );
	sha.Final( inner );
	for ( int i = 0; i < sqSha256::BLOCK_SIZE; i++ ) {
		pad[ i ] = keyBlock[ i ] ^ 0x5c;
	}
	sha.Reset();
	sha.Update( pad, sizeof( pad ) );
	sha.Update( inner, sizeof( inner ) );
	sha.Final( mac );
}

// compares in time that does not depend on where the inputs first differ
inline bool SQ_ConstantTimeEqual( const uint8_t* a, const uint8_t* b, int size ) {
	uint8_t diff = 0;
	for ( int i = 0; i < size; i++ ) {
		diff |= static_cast< uint8_t >( a[ i ] ^ b[ i ] );
	}
	return diff == 0;
}

// the number of leading zero bits of a digest
inline int SQ_LeadingZeroBits( const uint8_t* digest, int size ) {
	int bits = 0;
	for ( int i = 0; i < size; i++ ) {
		if ( digest[ i ] == 0 ) {
			bits += 8;
			continue;
		}
		uint8_t b = digest[ i ];
		while ( ( b & 0x80 ) == 0 ) {
			bits++;
			b = static_cast< uint8_t >( b << 1 );
		}
		break;
	}
	return bits;
}

#endif /* !__ASYNC_SHA256_H__ */
