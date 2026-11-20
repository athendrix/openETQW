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

#endif /* !__SYS_LOCALSUBNETS_H__ */
