/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
jech/dht (src/libs/dht/dht.c, unchanged), compiled for Windows Vista or later, where
Winsock has inet_ntop; dht.c itself targets Windows XP when nothing says otherwise.
*/
#if defined( _WIN32 ) && !defined( _WIN32_WINNT )
#define _WIN32_WINNT 0x0601
#endif
#include "../libs/dht/dht.c"
