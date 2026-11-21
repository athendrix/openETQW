<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# dht

`dht.c` and `dht.h` are Juliusz Chroboczek's implementation of the BitTorrent
Mainline DHT (BEP 5), unchanged from https://github.com/jech/dht at commit
`560f7c169c0e5d78e36a44cd1a5d7bd7608013e7` (MIT licence:
`LICENSES/third-party/jech-dht.txt`).

| File | SHA-256 |
|---|---|
| `dht.c` | `92bd036176468ff883906d68e301ab35b1f68e651cfee01d92a2c9f01af2978c` |
| `dht.h` | `6aaef163eefb948dfe1d9f6b7fefe8fb2392dad86fa6301967693c0171130b6f` |

The engine's server discovery uses it (`src/sdnet/Dht.cpp`, which gives it the
functions it needs from its user: sending, hashing, random bytes and the blacklist).
