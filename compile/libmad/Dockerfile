# libmad: MP3 decoding. Last real upstream release is 0.15.1b (2004,
# via the SourceForge "mad" project) — no GitHub-hosted tags exist to
# track a "latest" version against (checked: github.com/markjeee/libmad
# is an unofficial mirror with zero tags), so this is a fixed pin, not a
# latest-tracking one, same treatment as a genuinely dead upstream.
FROM kirigami-audiowaveform-wasm:base

ARG LIBMAD_VERSION

# CONFIRMED at the first real build (2026-09-12): configure correctly
# selects the portable FPM_DEFAULT fixed-point math path (not an
# x86/ARM-asm one) — "checking for architecture-specific fixed-point math
# routines... configure: WARNING: default fixed-point math will yield
# limited accuracy" is expected and harmless here (autoconf's own generic
# warning whenever it can't find a hand-tuned asm path for the detected
# host, exactly what we want for wasm32).
RUN wget "https://sourceforge.net/projects/mad/files/libmad/$LIBMAD_VERSION/libmad-$LIBMAD_VERSION.tar.gz/download" -O libmad.tar.gz && \
    tar xf libmad.tar.gz && \
    mv libmad-$LIBMAD_VERSION /root/libmad

# libmad's bundled config.sub (from 2004) predates the
# wasm32-unknown-emscripten triplet and rejects it outright ("Invalid
# configuration ... system `emscripten' not recognized") — confirmed on
# the first real build, same issue php-wasm-compiler's libyaml/Dockerfile
# hit. Fixed via a real patch (not an inline overwrite) replacing it with
# a modern (2022) upstream config.sub that does recognize the triplet —
# verified applying cleanly with both `git apply --check` and
# `patch -p1 --dry-run` against a pristine libmad-0.15.1b checkout before
# being committed, same discipline as patches/audiowaveform/.
COPY ./patches/libmad/config.sub-wasm32-emscripten.patch /root/config.sub.patch
RUN cd /root/libmad && git apply --no-index -v /root/config.sub.patch

# CFLAGS="-O2": libmad's configure, when it detects a gcc-like compiler,
# hardcodes a set of GCC-specific 1990s-era optimization flags
# (-fforce-mem, -fforce-addr, -fthread-jumps, -fcse-follow-jumps,
# -fcse-skip-blocks, -fregmove, ...) into the generated Makefile's CFLAGS.
# clang (which emcc wraps) rejects several of these outright ("unknown
# argument") rather than just warning — confirmed on the first real build.
# Passing CFLAGS explicitly here overrides that autodetected default
# (autoconf only fills in its own default when the CFLAGS env var isn't
# already set).
RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/libmad && \
    emconfigure ./configure \
        --host=wasm32-unknown-emscripten \
        --prefix=/root/lib \
        --disable-shared \
        --enable-static \
        CFLAGS="-O2" && \
    emmake make && \
    emmake make install
