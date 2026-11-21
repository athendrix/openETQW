// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: CvarMacroTest.cpp
"$name" cvar expansion in commands (src/framework/CvarMacro.h): a known cvar becomes its
text as one quoted argument, nothing expands inside quotes, unknown names and lone '$'
stay, and a retail-style map rotation chains through g_nextMap.
===========================================================================
*/

#include "src/framework/CvarMacro.h"

#include <cstdio>
#include <map>

static int g_failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		fprintf( stderr, "FAIL: %s\n", what );
		g_failures++;
	}
}

static std::map< std::string, std::string > cvars;

static bool Lookup( const char* name, std::string& value ) {
	std::map< std::string, std::string >::const_iterator it = cvars.find( name );
	if ( it == cvars.end() ) {
		return false;
	}
	value = it->second;
	return true;
}

static std::string Expand( const char* text ) {
	return CVM_Expand( text, Lookup );
}

int main() {
	cvars[ "m1" ] = "set g_nextMap $m0;spawnServer campaign_northeurope";
	cvars[ "map" ] = "valley";
	cvars[ "q" ] = "a \"b\" c";

	Expect( Expand( "set g_nextMap $m1" ) == "set g_nextMap \"set g_nextMap $m0;spawnServer campaign_northeurope\"", "a cvar becomes one quoted argument, not expanded again" );
	Expect( Expand( "spawnServer $map" ) == "spawnServer \"valley\"", "a simple value" );
	Expect( Expand( "set m0 \"set g_nextMap $m1;spawnServer x\"" ) == "set m0 \"set g_nextMap $m1;spawnServer x\"", "nothing expands inside quotes" );
	Expect( Expand( "echo $nothere" ) == "echo $nothere", "an unknown name stays" );
	Expect( Expand( "echo $ 5 $5 a$" ) == "echo $ 5 $5 a$", "a lone or numeric $ stays" );
	Expect( Expand( "say $map$map" ) == "say \"valley\"\"valley\"", "two in a row" );
	Expect( Expand( "echo $q" ) == "echo \"a 'b' c\"", "a quote inside a value cannot end the argument" );
	Expect( Expand( "" ) == "" && Expand( NULL ) == "", "empty and NULL" );

	if ( g_failures == 0 ) {
		printf( "CvarMacroTest: all checks passed\n" );
	}
	return g_failures == 0 ? 0 : 1;
}
