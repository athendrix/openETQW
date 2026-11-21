// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SDNET_LANDISCOVERY_H__
#define __SDNET_LANDISCOVERY_H__

/*
Finding servers on the local network (SDNetBrowser.cpp).

A LAN search sends the server query's first step to the server port (27733) as a broadcast
to 255.255.255.255 and, for each interface, to the subnet's directed broadcast address.
Some networks drop broadcasts (Wi-Fi client isolation, guest networks, some VPNs), so the
browser also sweeps the subnets slowly host by host: lanSweep_t makes the list of hosts.

Only private subnets (10/8, 172.16/12, 192.168/16) are swept, and at most maxHosts
addresses: a subnet bigger than that is swept only in the block of that size around our
own address. The hosts of our own /24 go first, since a small home or LAN-party network
is the common case.

Addresses here are IPv4 in host byte order. Plain C++, no engine headers, so the
asset-free unit suite can check it (tests/LanDiscoveryTest.cpp).
*/

#include <cstdint>
#include <vector>

struct lanSubnet_t {
	uint32_t	address;	// this host's address on the subnet
	uint32_t	mask;
};

// the subnet's directed broadcast address, or 0 when it has none (/31, /32)
inline uint32_t LAN_DirectedBroadcast( const lanSubnet_t& subnet ) {
	if ( ( ~subnet.mask ) < 3u ) {
		return 0;
	}
	return ( subnet.address & subnet.mask ) | ~subnet.mask;
}

inline bool LAN_IsPrivate( uint32_t address ) {
	return ( address >> 24 ) == 10 ||
		( address >> 20 ) == ( ( 172u << 4 ) | 1u ) ||		// 172.16/12
		( address >> 16 ) == ( ( 192u << 8 ) | 168u );		// 192.168/16
}

// a netmask is contiguous ones then zeros
inline bool LAN_IsValidMask( uint32_t mask ) {
	const uint32_t inverted = ~mask;
	return ( inverted & ( inverted + 1 ) ) == 0;
}

/*
The hosts to sweep, in order: no duplicates, never our own addresses, never a subnet's
network or broadcast address.
*/
inline void LAN_SweepTargets( const lanSubnet_t* subnets, int numSubnets, int maxHosts, std::vector< uint32_t >& out ) {
	out.clear();
	if ( maxHosts <= 0 ) {
		return;
	}
	for ( int s = 0; s < numSubnets && static_cast< int >( out.size() ) < maxHosts; s++ ) {
		const lanSubnet_t& subnet = subnets[ s ];
		if ( !LAN_IsValidMask( subnet.mask ) || !LAN_IsPrivate( subnet.address ) || LAN_DirectedBroadcast( subnet ) == 0 ) {
			continue;
		}
		const uint32_t network = subnet.address & subnet.mask;
		const uint32_t broadcast = network | ~subnet.mask;

		// the block to sweep: the whole subnet, or the aligned block of at most maxHosts
		// addresses (a power of two) that holds our address
		uint64_t blockSize = static_cast< uint64_t >( ~subnet.mask ) + 1;
		uint64_t limit = 1;
		while ( limit * 2 <= static_cast< uint64_t >( maxHosts ) + 2 ) {
			limit *= 2;
		}
		if ( blockSize > limit ) {
			blockSize = limit;
		}
		const uint32_t blockMask = ~static_cast< uint32_t >( blockSize - 1 );
		const uint32_t blockStart = subnet.address & blockMask;

		// our /24 (within the block) first, then the rest of the block
		const uint32_t ownStart = blockSize > 256 ? ( subnet.address & 0xffffff00u ) : blockStart;
		const uint32_t ownSize = blockSize > 256 ? 256u : static_cast< uint32_t >( blockSize );
		for ( int pass = 0; pass < 2; pass++ ) {
			const uint32_t start = pass == 0 ? ownStart : blockStart;
			const uint64_t size = pass == 0 ? ownSize : blockSize;
			for ( uint64_t i = 0; i < size && static_cast< int >( out.size() ) < maxHosts; i++ ) {
				const uint32_t host = start + static_cast< uint32_t >( i );
				if ( pass == 1 && host >= ownStart && host - ownStart < ownSize ) {
					continue;	// swept in the first pass
				}
				if ( host == network || host == broadcast ) {
					continue;
				}
				bool skip = false;
				for ( int o = 0; o < numSubnets && !skip; o++ ) {
					skip = subnets[ o ].address == host;
				}
				for ( size_t d = 0; d < out.size() && !skip && s > 0; d++ ) {
					skip = out[ d ] == host;	// subnets can overlap
				}
				if ( !skip ) {
					out.push_back( host );
				}
			}
		}
	}
}

#endif /* !__SDNET_LANDISCOVERY_H__ */
