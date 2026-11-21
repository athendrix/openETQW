// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SDNET_DHT_H__
#define __SDNET_DHT_H__

/*
Server discovery through the BitTorrent Mainline DHT (BEP 5), with jech/dht
(src/libs/dht). Servers that announce (net_serverAnnounce) store their address under
one key, the SHA-1 of "Enemy Territory Quake Wars"; the browser asks the DHT for the
addresses under it (Dht.cpp).

jech/dht keeps one node per process. It runs on an idPort the game already reads: a
dedicated server's game port, else the browser's port; whoever reads that port hands
the DHT's packets (bencoded: 'd', a digit, ':') to DHT_Input. On a game server port
only packets no client or query claimed are the DHT's.
*/

class idPort;

typedef void ( *dhtPeerFunc_t )( const netadr_t& peer );

// starts the node on this port unless one runs already; true when it runs on this port
bool	DHT_Start( idPort* port );
// stops the node if it runs on this port, keeping its nodes for the next start
void	DHT_Stop( idPort* port );
bool	DHT_RunningOn( const idPort* port );
bool	DHT_Running( void );

bool	DHT_IsPacket( const void* data, int size );
void	DHT_Input( const netadr_t& from, const void* data, int size );
void	DHT_Frame( void );

// announce this game port (0: stop announcing), now and every 15 minutes
void	DHT_SetAnnouncePort( int port );
// look for servers; each address found goes to the peer function
void	DHT_Search( void );
void	DHT_SetPeerFunc( dhtPeerFunc_t func );
// asks a node at this address to join (dhtPing)
void	DHT_AddNode( const netadr_t& address );

void	DHT_RegisterCommands( void );

#endif /* !__SDNET_DHT_H__ */
