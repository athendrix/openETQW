<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# TweetNaCl

`tweetnacl.c` and `tweetnacl.h` are TweetNaCl 20140427 by Daniel J. Bernstein, Bernard
van Gastel, Wesselin Janssen, Tanja Lange, Peter Schwabe and Sjaak Smetsers, from
https://tweetnacl.cr.yp.to/20140427/ (public domain, as the site states).

`tweetnacl.h` is unchanged. `tweetnacl.c` has one change: two left shifts of values that
can be negative, which C leaves undefined (UBSan stops on them), are written as the
multiplications they stand for, the same result on every two's complement machine:

- `car25519`: `o[i]-=c<<16;` is `o[i]-=c*65536;`
- `modL`: `x[j] -= carry << 8;` is `x[j] -= carry * 256;`

| File | SHA-256 upstream | SHA-256 here |
|---|---|---|
| `tweetnacl.c` | `02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4` | `6c88d4e8c75200cb9a236bd9c8d6ab03172ed8484628032a6e172875cee31620` |
| `tweetnacl.h` | `43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287` | the same |

The engine uses its Ed25519 signatures (`crypto_sign`) for server identity
(`src/framework/async/ServerIdentity.h`). TweetNaCl needs a `randombytes` from its user;
`src/framework/async/ServerIdentity.cpp` gives it the OS's random source.
