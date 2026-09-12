# libsndfile: WAV/AIFF/RAW/FLAC/Ogg-Vorbis/Ogg-Opus decoding
# (SndFileAudioFileReader).
#
# v1 scope correction (CLAUDE.md decision 9 originally assumed FLAC/Ogg/
# Opus came "at no extra architectural cost" via libsndfile alone — wrong.
# libsndfile only reads those formats when built against libFLAC/libogg/
# libvorbis/libopus. Those four are now vendored (compile/libogg,
# compile/libflac, compile/libvorbis, compile/libopus) and merged into this
# same /root/lib tree below.
#
# Real build/runtime bug hit and fixed here (2026-09-12): with
# ENABLE_EXTERNAL_LIBS=ON alone, `make audiowaveform-wasm` and every
# individual lib build succeeded (no errors), but real FLAC/Ogg files
# failed at runtime with libsndfile's own "File contains data in an
# unimplemented format." Root-caused with a debug build (binding.cpp's
# output_stream/error_stream temporarily pointed at std::cerr) plus
# inspecting the built image's own CMakeCache.txt/config.h directly:
# `pkg_check_modules` DID find flac/ogg/vorbis/opus (PC_FLAC_FOUND=1 etc.,
# confirmed in CMakeCache.txt — the base image's
# PKG_CONFIG_PATH=/root/lib/lib/pkgconfig works fine for that), but the
# subsequent `find_library`/`find_path` calls in libsndfile's own
# cmake/FindFLAC.cmake etc. still came back NOTFOUND, because Emscripten's
# own toolchain file sets CMAKE_FIND_ROOT_PATH_MODE_LIBRARY/INCLUDE=ONLY,
# which restricts find_library/find_path — even ones given explicit HINTS
# from the successful pkg-config lookup — to searching only inside
# CMAKE_FIND_ROOT_PATH (the Emscripten sysroot), not /root/lib where our
# vendored libs actually live. Net effect: config.h's own
# HAVE_EXTERNAL_XIPH_LIBS ended up "0" silently (CMakeLists.txt's own
# fallback: `if (ENABLE_EXTERNAL_LIBS AND NOT (Vorbis_FOUND OR FLAC_FOUND OR
# OPUS_FOUND)) set (ENABLE_EXTERNAL_LIBS OFF)`), even though flac.c/
# ogg_vorbis.c/ogg_opus.c were still compiled as stub/no-op format handlers
# (which is why the *build* looked clean — no missing-file or link error,
# just silently non-functional at runtime).
#
# Fixed with the standard Emscripten-documented idiom for exactly this
# situation: appending our own lib prefix to CMAKE_FIND_ROOT_PATH via
# -DCMAKE_FIND_ROOT_PATH=/root/lib below. Passed as a cache variable, the
# Emscripten toolchain file's own `list(APPEND CMAKE_FIND_ROOT_PATH
# "${EMSCRIPTEN_SYSROOT}")` appends the sysroot onto our value rather than
# replacing it, so both /root/lib and the sysroot get searched. Verified
# fixed for real (not guessed): FLAC_FOUND/Vorbis_FOUND/OGG_FOUND/
# OPUS_FOUND all resolved, HAVE_EXTERNAL_XIPH_LIBS became 1 in the rebuilt
# config.h, and the format-matrix smoke test's FLAC/Ogg-Vorbis/Ogg-Opus
# cases went from a `null` result to real decoded peaks.
#
# ENABLE_MPEG=OFF deliberately: recent libsndfile can also read/write MP3
# via mpg123/lame, but this project already has a real MP3 path via
# libmad/libid3tag (audiowaveform's own Mp3AudioFileReader) — no reason to
# vendor a second, redundant MP3 codec stack.
FROM kirigami-audiowaveform-wasm:base

ARG LIBSNDFILE_VERSION

COPY --from=kirigami-audiowaveform-wasm:libogg /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libflac /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libvorbis /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libopus /root/lib /root/lib

RUN wget "https://github.com/libsndfile/libsndfile/releases/download/$LIBSNDFILE_VERSION/libsndfile-$LIBSNDFILE_VERSION.tar.xz" -O libsndfile.tar.xz && \
    tar xf libsndfile.tar.xz && \
    mv libsndfile-$LIBSNDFILE_VERSION /root/libsndfile

RUN set -euxo pipefail; \
    source /root/emsdk/emsdk_env.sh; \
    cd /root/libsndfile; \
    mkdir -p build && cd build && \
    emcmake cmake -G"Unix Makefiles" .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/root/lib \
        -DCMAKE_FIND_ROOT_PATH=/root/lib \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_PROGRAMS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_TESTING=OFF \
        -DENABLE_EXTERNAL_LIBS=ON \
        -DENABLE_MPEG=OFF \
        -DENABLE_CPACK=OFF

RUN source /root/emsdk/emsdk_env.sh && \
    emmake make -C /root/libsndfile/build && \
    emmake make -C /root/libsndfile/build install
