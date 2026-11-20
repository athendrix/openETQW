// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SDNET_SERVERCACHE_H__
#define __SDNET_SERVERCACHE_H__

/*
The servers the browser has heard of from the DHT (Dht.cpp), kept between runs in the
save path (servercache.dat, SDNetBrowser.cpp), so the Internet tab is filled at once,
before the DHT has been asked again.

Every Internet search asks every server here; only those that answer are listed, so a
server that does not answer is hidden, not forgotten. A server is forgotten:
  - when it once answered, at its first unanswered query more than KEEP_SECONDS (a month)
    after its last answer: that query was its last chance;
  - when it never answered, at an unanswered query more than NEW_GRACE_SECONDS after the
    DHT last named it (the DHT names any address anyone stored, so an address that
    never answers is let go sooner);
  - when the cache is full: the one heard of longest ago that never answered, else the
    one that answered longest ago.

Times are seconds of the wall clock (time()). Plain C++, no engine headers, so the
asset-free unit suite can check it (tests/ServerCacheTest.cpp).
*/

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

class sdServerCache {
public:
	static const int		MAX_ENTRIES			= 2048;
	static const int		MAX_ADDRESS_LENGTH	= 63;
	static const int64_t	KEEP_SECONDS		= 30 * 24 * 3600;
	static const int64_t	NEW_GRACE_SECONDS	= 24 * 3600;

	struct entry_t {
		std::string	address;		// Sys_NetAdrToString's form
		int64_t		lastAnswered;	// 0: never
		int64_t		lastHeard;		// the DHT named it, or it answered
	};

	int Num() const { return static_cast< int >( entries.size() ); }
	const entry_t& operator[]( int index ) const { return entries[ index ]; }

	const entry_t* Find( const char* address ) const {
		const int index = IndexOf( address );
		return index >= 0 ? &entries[ index ] : NULL;
	}

	// the DHT named the address
	void Heard( const char* address, int64_t now ) {
		if ( !ValidAddress( address ) ) {
			return;
		}
		const int index = IndexOf( address );
		if ( index >= 0 ) {
			entries[ index ].lastHeard = now;
			return;
		}
		MakeRoom();
		entry_t entry;
		entry.address = address;
		entry.lastAnswered = 0;
		entry.lastHeard = now;
		entries.push_back( entry );
	}

	// the server answered a query
	void Answered( const char* address, int64_t now ) {
		if ( !ValidAddress( address ) ) {
			return;
		}
		int index = IndexOf( address );
		if ( index < 0 ) {
			MakeRoom();
			entry_t entry;
			entry.address = address;
			entries.push_back( entry );
			index = static_cast< int >( entries.size() ) - 1;
		}
		entries[ index ].lastAnswered = now;
		entries[ index ].lastHeard = now;
	}

	// a query went unanswered; true when the server is forgotten (the module comment)
	bool Unanswered( const char* address, int64_t now ) {
		const int index = IndexOf( address );
		if ( index < 0 ) {
			return false;
		}
		const entry_t& entry = entries[ index ];
		const bool forget = entry.lastAnswered > 0 ?
			now - entry.lastAnswered > KEEP_SECONDS :
			now - entry.lastHeard > NEW_GRACE_SECONDS;
		if ( forget ) {
			entries.erase( entries.begin() + index );
		}
		return forget;
	}

	// what an Internet search asks, in order: those that answered, the latest first, then
	// the rest, the latest heard of first
	void ToAsk( std::vector< std::string >& out ) const {
		std::vector< const entry_t* > answered, unanswered;
		for ( size_t i = 0; i < entries.size(); i++ ) {
			( entries[ i ].lastAnswered > 0 ? answered : unanswered ).push_back( &entries[ i ] );
		}
		SortBy( answered, true );
		SortBy( unanswered, false );
		out.clear();
		for ( size_t i = 0; i < answered.size(); i++ ) {
			out.push_back( answered[ i ]->address );
		}
		for ( size_t i = 0; i < unanswered.size(); i++ ) {
			out.push_back( unanswered[ i ]->address );
		}
	}

	// the file: a header line, then "address lastAnswered lastHeard" per line
	std::string Write() const {
		std::string text = Header();
		text += "\n";
		for ( size_t i = 0; i < entries.size(); i++ ) {
			char numbers[ 64 ];
			FormatNumbers( entries[ i ].lastAnswered, entries[ i ].lastHeard, numbers );
			text += entries[ i ].address;
			text += numbers;
			text += "\n";
		}
		return text;
	}

	// reads Write's text; lines that are not an entry are skipped. False without the header
	bool Read( const char* text ) {
		entries.clear();
		const size_t headerLength = strlen( Header() );
		if ( text == NULL || strncmp( text, Header(), headerLength ) != 0 || ( text[ headerLength ] != '\n' && text[ headerLength ] != '\r' ) ) {
			return false;
		}
		const char* line = strchr( text, '\n' );
		while ( line != NULL && *line != '\0' ) {
			line++;
			const char* end = strchr( line, '\n' );
			const std::string current( line, end != NULL ? static_cast< size_t >( end - line ) : strlen( line ) );
			ReadLine( current );
			line = end;
		}
		return true;
	}

	void Clear() { entries.clear(); }

private:
	static const char* Header() { return "openETQW server cache 1"; }

	int IndexOf( const char* address ) const {
		if ( address == NULL ) {
			return -1;
		}
		for ( size_t i = 0; i < entries.size(); i++ ) {
			if ( entries[ i ].address == address ) {
				return static_cast< int >( i );
			}
		}
		return -1;
	}

	static bool ValidAddress( const char* address ) {
		if ( address == NULL || address[ 0 ] == '\0' || strlen( address ) > static_cast< size_t >( MAX_ADDRESS_LENGTH ) ) {
			return false;
		}
		for ( const char* c = address; *c != '\0'; c++ ) {
			if ( *c <= ' ' || *c > '~' ) {
				return false;
			}
		}
		return true;
	}

	void MakeRoom() {
		if ( static_cast< int >( entries.size() ) < MAX_ENTRIES ) {
			return;
		}
		// the never-answered heard of longest ago, else the answered longest ago
		int victim = -1;
		for ( int pass = 0; pass < 2 && victim < 0; pass++ ) {
			for ( size_t i = 0; i < entries.size(); i++ ) {
				const entry_t& entry = entries[ i ];
				if ( ( pass == 0 ) != ( entry.lastAnswered == 0 ) ) {
					continue;
				}
				const int64_t age = pass == 0 ? entry.lastHeard : entry.lastAnswered;
				if ( victim < 0 || age < ( pass == 0 ? entries[ victim ].lastHeard : entries[ victim ].lastAnswered ) ) {
					victim = static_cast< int >( i );
				}
			}
		}
		entries.erase( entries.begin() + victim );
	}

	static void SortBy( std::vector< const entry_t* >& list, bool byAnswer ) {
		// insertion sort, newest first; stable for equal times
		for ( size_t i = 1; i < list.size(); i++ ) {
			const entry_t* item = list[ i ];
			const int64_t key = byAnswer ? item->lastAnswered : item->lastHeard;
			size_t j = i;
			while ( j > 0 && ( byAnswer ? list[ j - 1 ]->lastAnswered : list[ j - 1 ]->lastHeard ) < key ) {
				list[ j ] = list[ j - 1 ];
				j--;
			}
			list[ j ] = item;
		}
	}

	static void FormatNumbers( int64_t a, int64_t b, char out[ 64 ] ) {
		char first[ 24 ], second[ 24 ];
		FormatInt( a, first );
		FormatInt( b, second );
		std::string text = " ";
		text += first;
		text += " ";
		text += second;
		memcpy( out, text.c_str(), text.size() + 1 );
	}

	static void FormatInt( int64_t value, char out[ 24 ] ) {
		char digits[ 24 ];
		int length = 0;
		const bool negative = value < 0;
		uint64_t magnitude = negative ? static_cast< uint64_t >( -( value + 1 ) ) + 1 : static_cast< uint64_t >( value );
		do {
			digits[ length++ ] = static_cast< char >( '0' + magnitude % 10 );
			magnitude /= 10;
		} while ( magnitude > 0 );
		int at = 0;
		if ( negative ) {
			out[ at++ ] = '-';
		}
		while ( length > 0 ) {
			out[ at++ ] = digits[ --length ];
		}
		out[ at ] = '\0';
	}

	static bool ParseInt( const std::string& text, int64_t& out ) {
		if ( text.empty() || text.size() > 19 ) {
			return false;
		}
		int64_t value = 0;
		for ( size_t i = 0; i < text.size(); i++ ) {
			if ( text[ i ] < '0' || text[ i ] > '9' ) {
				return false;
			}
			value = value * 10 + ( text[ i ] - '0' );
		}
		out = value;
		return true;
	}

	void ReadLine( std::string line ) {
		while ( !line.empty() && ( line[ line.size() - 1 ] == '\r' || line[ line.size() - 1 ] == ' ' ) ) {
			line.erase( line.size() - 1 );
		}
		const size_t first = line.find( ' ' );
		const size_t second = first == std::string::npos ? std::string::npos : line.find( ' ', first + 1 );
		if ( second == std::string::npos || line.find( ' ', second + 1 ) != std::string::npos ) {
			return;
		}
		entry_t entry;
		entry.address = line.substr( 0, first );
		if ( !ValidAddress( entry.address.c_str() ) || IndexOf( entry.address.c_str() ) >= 0 ||
			!ParseInt( line.substr( first + 1, second - first - 1 ), entry.lastAnswered ) ||
			!ParseInt( line.substr( second + 1 ), entry.lastHeard ) ) {
			return;
		}
		if ( static_cast< int >( entries.size() ) >= MAX_ENTRIES ) {
			return;
		}
		entries.push_back( entry );
	}

	std::vector< entry_t >	entries;
};

#endif /* !__SDNET_SERVERCACHE_H__ */
