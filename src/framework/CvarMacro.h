// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __FRAMEWORK_CVARMACRO_H__
#define __FRAMEWORK_CVARMACRO_H__

/*
"$name" in a command: the cvar's text, as one quoted argument (CmdSystem.cpp expands each
command once, after the buffer splits it at ';' and line ends, before tokenizing).

Retail server configs chain map rotations with it:
	set m0 "set g_nextMap $m1;spawnServer campaign_northamerica"
	set m1 "set g_nextMap $m0;spawnServer campaign_northeurope"
	vstr m0
Inside quotes nothing expands, so m0 keeps "$m1"; running m0 sets g_nextMap to m1's text,
whose "$m0" waits until g_nextMap runs. A name that is not a cvar stays as written.

Plain C++ with a lookup function, so the asset-free unit suite can check it
(tests/CvarMacroTest.cpp).
*/

#include <string>

// lookup( name, value ) is true when name is a cvar, with its text in value
template< typename Lookup >
std::string CVM_Expand( const char* text, Lookup lookup ) {
	std::string out;
	if ( text == NULL ) {
		return out;
	}
	bool quoted = false;
	for ( const char* c = text; *c != '\0'; ) {
		if ( *c == '"' ) {
			quoted = !quoted;
			out += *c++;
			continue;
		}
		const bool nameStart = c[ 1 ] == '_' || ( c[ 1 ] >= 'a' && c[ 1 ] <= 'z' ) || ( c[ 1 ] >= 'A' && c[ 1 ] <= 'Z' );
		if ( quoted || *c != '$' || !nameStart ) {
			out += *c++;
			continue;
		}
		const char* end = c + 1;
		while ( *end == '_' || ( *end >= 'a' && *end <= 'z' ) || ( *end >= 'A' && *end <= 'Z' ) || ( *end >= '0' && *end <= '9' ) ) {
			end++;
		}
		const std::string name( c + 1, end );
		std::string value;
		if ( !lookup( name.c_str(), value ) ) {
			out.append( c, end );
			c = end;
			continue;
		}
		// one argument; a quote inside would end it, so it becomes an apostrophe
		out += '"';
		for ( size_t i = 0; i < value.size(); i++ ) {
			out += value[ i ] == '"' ? '\'' : value[ i ];
		}
		out += '"';
		c = end;
	}
	return out;
}

#endif /* !__FRAMEWORK_CVARMACRO_H__ */
