# nestegg (Mozilla): WebM/Matroska demuxer — the piece this project needs
# for WebM audio support (decision 19). Pulls raw Vorbis/Opus packets out
# of the container; decoding itself reuses the libvorbis/libopus already
# vendored for Ogg Vorbis/Opus support (compile/libvorbis, compile/libopus)
# — same "one targeted lib at a time" philosophy as mp4read.c for M4A,
# rather than vendoring a general-purpose media framework. Genuinely
# minimal upstream too: a single include/nestegg/nestegg.h + a single
# src/nestegg.c.
#
# License: ISC (verified 2026-09-12 by fetching mozilla/nestegg's real
# LICENSE) — permissive, GPL-3.0-or-later compatible.
#
# No release tags exist upstream (matrix.json's nestegg note — AC_INIT
# pins a permanent "0.1git" version), so this is pinned to a commit SHA,
# and — unlike every other autotools-based lib in this project — the
# GitHub source archive at that SHA has no pre-generated `configure`
# script (only ever produced by a real release process, which never
# happened here), so autoreconf runs here explicitly. automake/autoconf/
# libtool are already installed in the base image (base-image/Dockerfile).
FROM kirigami-audiowaveform-wasm:base

ARG NESTEGG_VERSION

RUN wget "https://github.com/mozilla/nestegg/archive/$NESTEGG_VERSION.tar.gz" -O nestegg.tar.gz && \
    tar xf nestegg.tar.gz && \
    mv nestegg-$NESTEGG_VERSION /root/nestegg

RUN cd /root/nestegg && autoreconf -fi

RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/nestegg && \
    emconfigure ./configure --prefix=/root/lib --disable-shared --enable-static && \
    emmake make && \
    emmake make install
