// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SYS_LOCALSUBNETS_H__
#define __SYS_LOCALSUBNETS_H__

#include "../sdnet/LanDiscovery.h"

/*
The IPv4 subnets of the interfaces that are up, not counting loopback, in host byte
order, for the LAN search (posix_net.cpp, win32/SystemBootstrap.cpp). Returns how many
were written to out.
*/
int Sys_GetLocalSubnets( lanSubnet_t* out, int max );

struct localAddress6_t {
	uint8_t			address[ 16 ];
	unsigned int	scope;		// the interface's index
};

/*
The IPv6 addresses of the interfaces that are up and can multicast, not counting
loopback: for the server's listed addresses (the global and unique local ones) and the
LAN search's multicast (one per interface, to ff02::1).
*/
int Sys_GetLocalIPv6( localAddress6_t* out, int max );

#endif /* !__SYS_LOCALSUBNETS_H__ */
