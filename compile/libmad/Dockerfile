# libmad: MP3 decoding. Last real upstream release is 0.15.1b (2004,
# via the SourceForge "mad" project) — no GitHub-hosted tags exist to
# track a "latest" version against (checked: github.com/markjeee/libmad
# is an unofficial mirror with zero tags), so this is a fixed pin, not a
# latest-tracking one, same treatment as a genuinely dead upstream.
FROM kirigami-audiowaveform-wasm:base

ARG LIBMAD_VERSION

# NOT YET VERIFIED AT AN ACTUAL BUILD (see CLAUDE.md decision 11 — no
# docker build has been run yet). Known risk area to check first: libmad's
# fixed.h selects a fixed-point math implementation (FPM_INTEL / FPM_ARM /
# FPM_DEFAULT / etc.) partly via hand-written inline assembly for real
# architectures. wasm32 shouldn't match any of the asm-using branches via
# plain autoconf host-triplet detection (we are NOT passing a
# -D__x86_64__-style architecture-spoofing flag the way php-wasm-compiler's
# pipeline does for SIMD-intrinics libs — see that repo's decision 32 for
# why that flag exists there and decision 3 here for why it doesn't apply)
# — but confirm FPM_DEFAULT (the portable C path) is what actually gets
# selected before trusting this build.
RUN wget "https://sourceforge.net/projects/mad/files/libmad/$LIBMAD_VERSION/libmad-$LIBMAD_VERSION.tar.gz/download" -O libmad.tar.gz && \
    tar xf libmad.tar.gz && \
    mv libmad-$LIBMAD_VERSION /root/libmad

RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/libmad && \
    emconfigure ./configure \
        --host=wasm32-unknown-emscripten \
        --prefix=/root/lib \
        --disable-shared \
        --enable-static && \
    emmake make && \
    emmake make install
