# zlib: hard, unconditional dependency of libid3tag's configure (it has no
# --without-zlib escape hatch — confirmed via `AC_CHECK_HEADER(zlib.h, [],
# [AC_MSG_ERROR(...)])` in its own configure.ac, first found by an actual
# build failure, 2026-09-12). Not needed by anything else in this project
# yet (libmad, libsndfile — built with ENABLE_EXTERNAL_LIBS=OFF — and
# audiowaveform's own core don't touch it).
FROM kirigami-audiowaveform-wasm:base

ARG LIBZ_VERSION

RUN wget "https://zlib.net/zlib-$LIBZ_VERSION.tar.gz" -O zlib.tar.gz && \
    tar xf zlib.tar.gz && \
    mv zlib-$LIBZ_VERSION /root/zlib
RUN chmod a+x /root/zlib/configure

RUN source /root/emsdk/emsdk_env.sh && \
    cd /root/zlib && \
    emconfigure ./configure --prefix=/root/lib --static && \
    emmake make && \
    emmake make install
