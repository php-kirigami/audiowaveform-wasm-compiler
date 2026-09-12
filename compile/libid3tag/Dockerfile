# libid3tag: ID3 tag parsing (audiowaveform's Mp3AudioFileReader needs it
# to skip ID3 tags before handing MP3 frames to libmad). Same SourceForge
# "mad" project as libmad, same fixed 0.15.1b pin — no GitHub tags to track
# a "latest" against either.
FROM kirigami-audiowaveform-wasm:base

ARG LIBID3TAG_VERSION

# CONFIRMED at the first real build (2026-09-12): libid3tag's configure
# hard-requires zlib.h (AC_CHECK_HEADER + AC_MSG_ERROR, no --without-zlib
# escape hatch — checked configure.ac directly). libz vendored specifically
# for this (see matrix.json's libz entry) — nothing else in this project
# needs it.
COPY --from=kirigami-audiowaveform-wasm:libz /root/lib /root/lib

RUN wget "https://sourceforge.net/projects/mad/files/libid3tag/$LIBID3TAG_VERSION/libid3tag-$LIBID3TAG_VERSION.tar.gz/download" -O libid3tag.tar.gz && \
    tar xf libid3tag.tar.gz && \
    mv libid3tag-$LIBID3TAG_VERSION /root/libid3tag

# Same fix as compile/libmad/Dockerfile (same real patch content — this is
# the same 2004-era SourceForge "mad" project, its bundled config.sub is
# byte-identical to libmad's own, confirmed with `diff -q` before
# generating the patch).
COPY ./patches/libid3tag/config.sub-wasm32-emscripten.patch /root/config.sub.patch
RUN cd /root/libid3tag && git apply --no-index -v /root/config.sub.patch

# CFLAGS="-DNDEBUG": same fix as compile/libmad/Dockerfile, for the same
# reason — confirmed by reading libid3tag's own configure.ac, which has
# the identical hand-written `set -- $CFLAGS` / `optimize=` argument-
# parsing pattern that traps a bare "-O2" (or autoconf's own unset-CFLAGS
# "-g -O2" default) into a set of GCC-specific flags clang/emcc rejects.
# See libmad/Dockerfile's comment for the full mechanism.
RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/libid3tag && \
    emconfigure ./configure \
        --host=wasm32-unknown-emscripten \
        --prefix=/root/lib \
        --disable-shared \
        --enable-static \
        CFLAGS="-DNDEBUG" \
        CPPFLAGS="-I/root/lib/include" \
        LDFLAGS="-L/root/lib/lib" && \
    emmake make && \
    emmake make install
