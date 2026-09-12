# libsndfile: WAV/AIFF/RAW decoding (SndFileAudioFileReader).
#
# v1 scope correction (CLAUDE.md decision 9 originally assumed FLAC/Ogg/
# Opus came "at no extra architectural cost" via libsndfile alone — wrong.
# libsndfile only reads those formats when built against libFLAC/libogg/
# libvorbis/libopus, none of which this project vendors yet. Building with
# ENABLE_EXTERNAL_LIBS=OFF here gets WAV/AIFF/RAW only; FLAC/Ogg/Opus are
# deferred, same "add the extra lib later" treatment as M4A/libfdk-aac.
#
# ENABLE_MPEG=OFF deliberately: recent libsndfile can also read/write MP3
# via mpg123/lame, but this project already has a real MP3 path via
# libmad/libid3tag (audiowaveform's own Mp3AudioFileReader) — no reason to
# vendor a second, redundant MP3 codec stack.
#
# NOT YET BUILT (CLAUDE.md decision 11).
FROM kirigami-audiowaveform-wasm:base

ARG LIBSNDFILE_VERSION

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
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_PROGRAMS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_TESTING=OFF \
        -DENABLE_EXTERNAL_LIBS=OFF \
        -DENABLE_MPEG=OFF \
        -DENABLE_CPACK=OFF

RUN source /root/emsdk/emsdk_env.sh && \
    emmake make -C /root/libsndfile/build && \
    emmake make -C /root/libsndfile/build install
