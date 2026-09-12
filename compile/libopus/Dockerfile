# libopus: the core Opus codec, needed for libsndfile's Ogg Opus support.
# Standalone at build time (no libogg dependency — libsndfile does its own
# Ogg-Opus demuxing using libogg directly, same way it does for Vorbis).
#
# License: 3-clause BSD-style, jointly held by Xiph.Org/Skype/Mozilla/etc.
# (verified 2026-09-12 by fetching xiph/opus's real COPYING) — permissive,
# GPL-3.0-or-later compatible. Bonus, unlike this project's AAC situation
# (CLAUDE.md decision 9's corrections): COPYING itself documents Opus's
# royalty-free IETF patent licenses (Xiph/Microsoft/Broadcom) — no patent
# ambiguity to carry forward the way AAC has.
#
# Real build failure hit and fixed here (2026-09-12): libopus's configure.ac
# runs its x86 SSE/SSE2/SSE4.1/AVX2 CPU-capability checks unconditionally,
# regardless of --host — it doesn't gate them on the target triplet the way
# a "should fall through to portable C" guess would assume. Cross-compiling
# with emconfigure hits `configure: error: no supported Get CPU Info method,
# please disable run-time CPU capabilities detection or intrinsics` because
# none of those x86 intrinsics compile under emcc/clang for wasm32. Fixed
# with the three flags configure's own --help documents for exactly this:
# --disable-rtcd (skip the runtime-dispatch CPU-detection machinery)
# --disable-intrinsics (skip the SSE/AVX/NEON-specific code paths)
# --disable-asm (skip hand-written assembly, same reasoning).
FROM kirigami-audiowaveform-wasm:base

ARG LIBOPUS_VERSION

# Same "v"-prefixed-tag/bare-filename situation as libogg/libvorbis.
RUN wget "https://github.com/xiph/opus/releases/download/v$LIBOPUS_VERSION/opus-$LIBOPUS_VERSION.tar.gz" -O opus.tar.gz && \
    tar xf opus.tar.gz && \
    mv opus-$LIBOPUS_VERSION /root/opus

RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/opus && \
    emconfigure ./configure --prefix=/root/lib --disable-shared --enable-static \
        --disable-rtcd --disable-intrinsics --disable-asm && \
    emmake make && \
    emmake make install
