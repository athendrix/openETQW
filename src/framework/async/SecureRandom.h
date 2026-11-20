// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __ASYNC_SECURERANDOM_H__
#define __ASYNC_SECURERANDOM_H__

/*
Unpredictable numbers for the server's secrets and challenges (std::random_device: the
OS's random source on Linux, macOS and Windows).

idLib's math header takes the C INFINITY macro away, and libc++'s <random> (macOS)
needs it, so it is put back around the include.
*/
#ifndef INFINITY
#define INFINITY __builtin_inff()
#define SECURE_RANDOM_DEFINED_INFINITY
#endif
#include <random>
#ifdef SECURE_RANDOM_DEFINED_INFINITY
#undef INFINITY
#undef SECURE_RANDOM_DEFINED_INFINITY
#endif

#include <cstdint>

inline uint32_t Sys_SecureRandom32( void ) {
	static std::random_device device;
	return static_cast< uint32_t >( device() );
}

#endif /* !__ASYNC_SECURERANDOM_H__ */
