# Ubuntu + Emscripten base image, shared by every third-party lib build and
# the final audiowaveform-core build. Adapted from php-wasm-compiler's own
# base-image (see that repo's CLAUDE.md) but trimmed: no JSPI/side-module
# emcc patching here (this project builds one monolithic Node wasm module,
# not a dylink MAIN_MODULE with side modules — see CLAUDE.md decision 3).
FROM ubuntu:noble AS emscripten

SHELL ["/bin/bash", "-c"]

ENV PKG_CONFIG_PATH=/root/lib/lib/pkgconfig

WORKDIR /root
RUN mkdir lib

RUN set -euxo pipefail; \
    export DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC; \
    apt-get update; \
    apt-get --no-install-recommends -y install \
    build-essential \
    automake \
    autoconf \
    libtool \
    pkgconf \
    make \
    git \
    ca-certificates \
    curl \
    wget \
    unzip \
    cmake \
    python3 \
    libboost-dev
# libboost-dev: only for boost::format (header-only, no lib to link) used
# by a few of audiowaveform's core files — see CLAUDE.md decision 8. No
# compiled Boost component (program_options/filesystem/regex) is needed.

# Same Emscripten version pin as php-wasm-compiler, so both Kirigami wasm
# pipelines stay on one known-good toolchain version. Not load-bearing here
# the way it is for PHP's MAIN_MODULE side-module ABI (no side modules in
# this project), but there is no reason to diverge either.
RUN ln -s /usr/bin/python3 /usr/bin/python
RUN bash -euxo pipefail <<'EOF'
    git clone https://github.com/emscripten-core/emsdk.git
    EMSDK_NOTTY=1 EMSDK_USE_CURL=1 ./emsdk/emsdk install --notty 4.0.19
    EMSDK_NOTTY=1 /root/emsdk/emsdk activate --notty 4.0.19
EOF

RUN mkdir -p /root/lib/lib /root/lib/include /root/lib/share
