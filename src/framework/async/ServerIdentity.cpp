// SPDX-License-Identifier: GPL-3.0-or-later
/*
TweetNaCl's randombytes (src/libs/tweetnacl): the OS's random source, or a saved seed
while SQ_IdentityFromSeed makes a key pair from it (ServerIdentity.h).
*/

#include <cmath>	// INFINITY, before SecureRandom.h

#include "SecureRandom.h"

#include <cstdint>
#include <cstring>

static const uint8_t* keySeed = NULL;

void SQ_SetKeySeed( const uint8_t* seed ) {
	keySeed = seed;
}

extern "C" void randombytes( unsigned char* out, unsigned long long size ) {
	if ( keySeed != NULL && size == 32 ) {
		memcpy( out, keySeed, 32 );
		return;
	}
	for ( unsigned long long i = 0; i < size; i += 4 ) {
		const uint32_t value = Sys_SecureRandom32();
		for ( unsigned long long j = 0; j < 4 && i + j < size; j++ ) {
			out[ i + j ] = static_cast< unsigned char >( value >> ( 8 * j ) );
		}
	}
}
