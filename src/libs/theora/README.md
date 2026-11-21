<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# theora

The decoder half of libtheora 1.2.0 (`lib/` and `include/theora/`), unchanged from
https://gitlab.xiph.org/xiph/theora at commit
`8e4808736e9c181b971306cc3f05df9e61354004` (BSD licence:
`LICENSES/third-party/libtheora.txt`, upstream's `COPYING`). The encoder's `.c` files
and the build system are left out; the encoder headers stay because the decoder's
headers include them.

It builds on the Ogg framing Doom 3 shipped (`src/sound/OggVorbis`, `meson.build`),
whose headers have no `ogg_uint64_t`; the build defines it for one loop in `state.c`.

The engine plays GUI movies with it (`src/sound/SoundTheora.cpp`): a `.ogv` beside a
`.bik` plays in its place on every platform.
