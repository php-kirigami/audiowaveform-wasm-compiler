# libfaad2 (knik0/faad2 fork, actively maintained): AAC decoding for M4A/AAC
# support (CLAUDE.md decision 9's correction #2). Builds only the core
# `faad` decoder library (floating-point, non-DRM — libfaad.a) via faad2's
# own top-level CMakeLists.txt; -DFAAD_BUILD_CLI=OFF skips the CLI frontend
# executable entirely, so no audio-output-backend dependencies to vendor.
#
# The frontend's mp4read.c/mp4read.h/unicode_support.c/unicode_support.h
# (the MP4 box demuxer feeding this decoder — not part of the installed lib,
# CMakeLists' FAAD_BUILD_CLI=OFF skips compiling them here too) are pulled
# directly from this image's /root/faad2/frontend source tree by
# ../audiowaveform/Dockerfile, which compiles and links them itself — same
# "pull only the specific files needed" approach this project already uses
# for bbc/audiowaveform's own source (CLAUDE.md decision 4/8).
#
# License, verified 2026-09-12 (not assumed — see CLAUDE.md decision 9's
# correction #2 for the original research): the core decoder
# (libfaad/decoder.c) is GPL-2.0-or-later; frontend/mp4read.c is
# GPL-3.0-or-later. Both compatible with this project's own
# GPL-3.0-or-later. AAC itself remains patent-encumbered technology
# independent of this choice of decoder — a real-world licensing
# consideration this project doesn't resolve, just doesn't make worse.
#
# NOT YET BUILT.
FROM kirigami-audiowaveform-wasm:base

ARG LIBFAAD2_VERSION

RUN wget "https://github.com/knik0/faad2/archive/refs/tags/$LIBFAAD2_VERSION.tar.gz" -O faad2.tar.gz && \
    tar xf faad2.tar.gz && \
    mv faad2-$LIBFAAD2_VERSION /root/faad2

RUN set -euxo pipefail; \
    source /root/emsdk/emsdk_env.sh; \
    cd /root/faad2; \
    mkdir -p build && cd build && \
    emcmake cmake -G"Unix Makefiles" .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/root/lib \
        -DBUILD_SHARED_LIBS=OFF \
        -DFAAD_BUILD_CLI=OFF

# Confirmed via faad2's real CMakeLists.txt: its own `install()` rules
# install both `faad` and `faad_drm` targets (libfaad.a/libfaad_drm.a) plus
# neaacdec.h and a faad2.pc — a normal `make install` works here, same as
# every other CMake-based lib in this project.
RUN source /root/emsdk/emsdk_env.sh && \
    emmake make -C /root/faad2/build && \
    emmake make -C /root/faad2/build install
