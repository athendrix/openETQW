<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# TweetNaCl

`tweetnacl.c` and `tweetnacl.h` are TweetNaCl 20140427 by Daniel J. Bernstein, Bernard
van Gastel, Wesselin Janssen, Tanja Lange, Peter Schwabe and Sjaak Smetsers, unchanged
from https://tweetnacl.cr.yp.to/20140427/ (public domain, as the site states).

| File | SHA-256 |
|---|---|
| `tweetnacl.c` | `02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4` |
| `tweetnacl.h` | `43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287` |

The engine uses its Ed25519 signatures (`crypto_sign`) for server identity
(`src/framework/async/ServerIdentity.h`). TweetNaCl needs a `randombytes` from its user;
`src/framework/async/ServerIdentity.cpp` gives it the OS's random source.
