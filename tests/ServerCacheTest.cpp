// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: ServerCacheTest.cpp
The browser's cache of servers found through the DHT (src/sdnet/ServerCache.h): a server
that answered is kept a month after its last answer and forgotten at the first unanswered
query after that; one that never answered is let go a day after the DHT last named it; a
full cache evicts the oldest never-answered first; the ask order; the file round trips and
skips malformed lines.
===========================================================================
*/

#include "src/sdnet/ServerCache.h"

#include <cstdio>

static int g_failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		fprintf( stderr, "FAIL: %s\n", what );
		g_failures++;
	}
}

int main() {
	const int64_t day = 24 * 3600;
	const int64_t t0 = 1800000000;
	sdServerCache cache;

	// an answering server survives unanswered queries for a month, then gets one last one
	cache.Answered( "198.51.100.1:27733", t0 );
	Expect( !cache.Unanswered( "198.51.100.1:27733", t0 + 10 * day ), "kept ten days after its answer" );
	Expect( !cache.Unanswered( "198.51.100.1:27733", t0 + 30 * day ), "kept a month after its answer" );
	Expect( cache.Find( "198.51.100.1:27733" ) != NULL, "still there" );
	Expect( cache.Unanswered( "198.51.100.1:27733", t0 + 30 * day + 1 ), "forgotten at the first unanswered query after a month" );
	Expect( cache.Find( "198.51.100.1:27733" ) == NULL, "gone" );

	// an answer resets the month
	cache.Answered( "198.51.100.2:27733", t0 );
	cache.Answered( "198.51.100.2:27733", t0 + 25 * day );
	Expect( !cache.Unanswered( "198.51.100.2:27733", t0 + 40 * day ), "a later answer keeps it" );

	// the DHT naming an answering server does not extend its month
	cache.Heard( "198.51.100.2:27733", t0 + 56 * day );
	Expect( cache.Unanswered( "198.51.100.2:27733", t0 + 56 * day ), "the DHT alone does not keep a server that stopped answering" );

	// a never-answering address is let go a day after the DHT last named it
	cache.Heard( "203.0.113.9:27733", t0 );
	Expect( !cache.Unanswered( "203.0.113.9:27733", t0 + day / 2 ), "a new address gets a day" );
	cache.Heard( "203.0.113.9:27733", t0 + day );
	Expect( !cache.Unanswered( "203.0.113.9:27733", t0 + day + day / 2 ), "named again, another day" );
	Expect( cache.Unanswered( "203.0.113.9:27733", t0 + 3 * day ), "let go" );
	Expect( !cache.Unanswered( "203.0.113.200:1", t0 ), "an unknown address is nothing to forget" );

	// bad addresses are refused
	cache.Clear();
	cache.Heard( "", t0 );
	cache.Heard( "has space:1", t0 );
	cache.Heard( "0123456789012345678901234567890123456789012345678901234567890123456789", t0 );
	cache.Heard( NULL, t0 );
	Expect( cache.Num() == 0, "bad addresses refused" );

	// the ask order: answered (latest first), then the rest (latest heard first)
	cache.Clear();
	cache.Heard( "[2001:db8::1]:27733", t0 + 5 );
	cache.Answered( "198.51.100.3:27733", t0 + 1 );
	cache.Heard( "203.0.113.4:27733", t0 + 9 );
	cache.Answered( "198.51.100.4:27733", t0 + 3 );
	std::vector< std::string > order;
	cache.ToAsk( order );
	Expect( order.size() == 4 && order[ 0 ] == "198.51.100.4:27733" && order[ 1 ] == "198.51.100.3:27733" &&
		order[ 2 ] == "203.0.113.4:27733" && order[ 3 ] == "[2001:db8::1]:27733", "ask order" );

	// the file
	const std::string text = cache.Write();
	sdServerCache copy;
	Expect( copy.Read( text.c_str() ) && copy.Num() == 4, "the file reads back" );
	const sdServerCache::entry_t* entry = copy.Find( "198.51.100.4:27733" );
	Expect( entry != NULL && entry->lastAnswered == t0 + 3 && entry->lastHeard == t0 + 3, "times read back" );
	entry = copy.Find( "[2001:db8::1]:27733" );
	Expect( entry != NULL && entry->lastAnswered == 0 && entry->lastHeard == t0 + 5, "an IPv6 entry reads back" );
	Expect( !copy.Read( "not a cache\n198.51.100.1:1 1 1\n" ) && copy.Num() == 0, "a file without the header is refused" );
	Expect( copy.Read( "openETQW server cache 1\r\n198.51.100.1:1 5 6\r\nbad line\n198.51.100.2:1 x 1\n198.51.100.3:1 1\n198.51.100.4:1 1 2 3\n198.51.100.1:1 7 8\n198.51.100.5:1 -1 2\n" ) &&
		copy.Num() == 1 && copy.Find( "198.51.100.1:1" )->lastAnswered == 5, "malformed and duplicate lines skipped, CRLF read" );

	// a full cache evicts the never-answered heard of longest ago, then the oldest answer
	cache.Clear();
	char address[ 32 ];
	for ( int i = 0; i < sdServerCache::MAX_ENTRIES; i++ ) {
		snprintf( address, sizeof( address ), "198.18.%d.%d:1", i / 256, i % 256 );
		if ( i % 2 == 0 ) {
			cache.Answered( address, t0 + i );
		} else {
			cache.Heard( address, t0 + i );
		}
	}
	Expect( cache.Num() == sdServerCache::MAX_ENTRIES, "full" );
	cache.Heard( "203.0.113.50:1", t0 + 99999 );
	Expect( cache.Num() == sdServerCache::MAX_ENTRIES && cache.Find( "198.18.0.1:1" ) == NULL && cache.Find( "198.18.0.0:1" ) != NULL, "evicts the oldest never-answered" );

	if ( g_failures == 0 ) {
		printf( "ServerCacheTest: all checks passed\n" );
	}
	return g_failures == 0 ? 0 : 1;
}
