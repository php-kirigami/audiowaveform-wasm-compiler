# libvorbis: Ogg Vorbis decoding for libsndfile. Vorbis audio is always
# Ogg-encapsulated, so this needs libogg (unlike libFLAC's native-FLAC-only
# build in compile/libflac/Dockerfile).
#
# License: Xiph.org's 3-clause BSD-style license (verified 2026-09-12 by
# fetching xiph/vorbis's real COPYING) — permissive, GPL-3.0-or-later
# compatible.
#
# NOT YET BUILT.
FROM kirigami-audiowaveform-wasm:base

ARG LIBVORBIS_VERSION

COPY --from=kirigami-audiowaveform-wasm:libogg /root/lib /root/lib

# Same "v"-prefixed-tag/bare-filename situation as libogg.
RUN wget "https://github.com/xiph/vorbis/releases/download/v$LIBVORBIS_VERSION/libvorbis-$LIBVORBIS_VERSION.tar.gz" -O libvorbis.tar.gz && \
    tar xf libvorbis.tar.gz && \
    mv libvorbis-$LIBVORBIS_VERSION /root/libvorbis

# --with-ogg=/root/lib: belt-and-braces alongside the base image's
# PKG_CONFIG_PATH=/root/lib/lib/pkgconfig (which ogg's own `make install`
# populates with ogg.pc) — configure.ac supports both discovery paths.
RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/libvorbis && \
    emconfigure ./configure --prefix=/root/lib --disable-shared --enable-static --with-ogg=/root/lib && \
    emmake make && \
    emmake make install
