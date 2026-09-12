# libid3tag: ID3 tag parsing (audiowaveform's Mp3AudioFileReader needs it
# to skip ID3 tags before handing MP3 frames to libmad). Same SourceForge
# "mad" project as libmad, same fixed 0.15.1b pin — no GitHub tags to track
# a "latest" against either.
FROM kirigami-audiowaveform-wasm:base

ARG LIBID3TAG_VERSION

# NOT YET VERIFIED AT AN ACTUAL BUILD (see CLAUDE.md decision 11). Known
# risk area to check first: libid3tag optionally links zlib to support
# compressed ID3v2 frames (rare in practice). This project doesn't vendor
# libz yet — if configure hard-requires zlib rather than treating it as an
# optional feature, this build will fail until a compile/libz/Dockerfile
# is added; if it's a soft/optional check, it should just build without
# compressed-frame support, which is an acceptable v1 limitation.
RUN wget "https://sourceforge.net/projects/mad/files/libid3tag/$LIBID3TAG_VERSION/libid3tag-$LIBID3TAG_VERSION.tar.gz/download" -O libid3tag.tar.gz && \
    tar xf libid3tag.tar.gz && \
    mv libid3tag-$LIBID3TAG_VERSION /root/libid3tag

# Same fix as compile/libmad/Dockerfile (same real patch content — this is
# the same 2004-era SourceForge "mad" project, its bundled config.sub is
# byte-identical to libmad's own, confirmed with `diff -q` before
# generating the patch).
COPY ./patches/libid3tag/config.sub-wasm32-emscripten.patch /root/config.sub.patch
RUN cd /root/libid3tag && git apply --no-index -v /root/config.sub.patch

# CFLAGS="-O2": added preemptively, not yet confirmed necessary for this
# lib specifically — libmad (same project, same era) hit clang rejecting
# its configure's default GCC-specific optimization flags outright, see
# compile/libmad/Dockerfile's comment for the exact flags and why.
RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/libid3tag && \
    emconfigure ./configure \
        --host=wasm32-unknown-emscripten \
        --prefix=/root/lib \
        --disable-shared \
        --enable-static \
        CFLAGS="-O2" && \
    emmake make && \
    emmake make install
