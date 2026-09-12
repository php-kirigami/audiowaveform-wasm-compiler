# libFLAC: native FLAC decoding for libsndfile. WITH_OGG=OFF deliberately —
# we only need to read plain .flac files, not Ogg-encapsulated FLAC, so this
# doesn't need libogg (unlike libvorbis, where Ogg encapsulation is mandatory).
#
# License: Xiph.org's 3-clause BSD-style license (verified 2026-09-12 by
# fetching xiph/flac's real COPYING.Xiph) — permissive, GPL-3.0-or-later
# compatible.
#
# NOT YET BUILT.
FROM kirigami-audiowaveform-wasm:base

ARG LIBFLAC_VERSION

# Unlike ogg/vorbis/opus, xiph/flac's GitHub release tags have no "v" prefix
# (e.g. "1.5.0", not "v1.5.0") — confirmed via the real release URL.
RUN wget "https://github.com/xiph/flac/releases/download/$LIBFLAC_VERSION/flac-$LIBFLAC_VERSION.tar.xz" -O flac.tar.xz && \
    tar xf flac.tar.xz && \
    mv flac-$LIBFLAC_VERSION /root/flac

RUN set -euxo pipefail; \
    source /root/emsdk/emsdk_env.sh; \
    cd /root/flac; \
    mkdir -p build && cd build && \
    emcmake cmake -G"Unix Makefiles" .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/root/lib \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_CXXLIBS=OFF \
        -DBUILD_PROGRAMS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_TESTING=OFF \
        -DBUILD_DOCS=OFF \
        -DINSTALL_MANPAGES=OFF \
        -DWITH_OGG=OFF

RUN source /root/emsdk/emsdk_env.sh && \
    emmake make -C /root/flac/build && \
    emmake make -C /root/flac/build install
