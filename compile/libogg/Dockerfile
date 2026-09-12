# libogg: Ogg container bitstream handling, needed by libvorbis (Vorbis is
# always Ogg-encapsulated) and by libsndfile's own Ogg/Opus demuxing (Opus
# audio files libsndfile reads are Ogg Opus, not raw Opus). Not needed by
# libFLAC here (WITH_OGG=OFF in compile/libflac/Dockerfile — we only need
# native .flac, not Ogg FLAC).
#
# License: Xiph.org's 3-clause BSD-style license (verified 2026-09-12 by
# fetching xiph/ogg's real COPYING) — permissive, GPL-3.0-or-later compatible.
#
# NOT YET BUILT.
FROM kirigami-audiowaveform-wasm:base

ARG LIBOGG_VERSION

# Real GitHub release tags are "v$VERSION" (e.g. v1.3.6), but the tarball
# filename itself has no "v" prefix — matrix.json stores the bare version
# (update-lib-versions.mjs's normalizeVersion() strips "v" from tags), so the
# "v" is added here, not stored in the matrix.
RUN wget "https://github.com/xiph/ogg/releases/download/v$LIBOGG_VERSION/libogg-$LIBOGG_VERSION.tar.gz" -O libogg.tar.gz && \
    tar xf libogg.tar.gz && \
    mv libogg-$LIBOGG_VERSION /root/libogg

RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/libogg && \
    emconfigure ./configure --prefix=/root/lib --disable-shared --enable-static && \
    emmake make && \
    emmake make install
