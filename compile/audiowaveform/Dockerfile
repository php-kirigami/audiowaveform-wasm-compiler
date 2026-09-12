# Final module: fetches audiowaveform's own source (unmodified — see
# CLAUDE.md decision 4/8, we extract, not fork), compiles only the
# confirmed-minimal core file set, links against libmad/libid3tag, and
# adds our own Embind wrapper (src/binding.cpp).
FROM kirigami-audiowaveform-wasm:base

ARG AUDIOWAVEFORM_VERSION

COPY --from=kirigami-audiowaveform-wasm:libmad /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libid3tag /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libsndfile /root/lib /root/lib

RUN wget "https://github.com/bbc/audiowaveform/archive/refs/tags/$AUDIOWAVEFORM_VERSION.tar.gz" -O audiowaveform.tar.gz && \
    tar xf audiowaveform.tar.gz && \
    mv audiowaveform-$AUDIOWAVEFORM_VERSION /root/audiowaveform

COPY ./compile/src/binding.cpp /root/audiowaveform/src/binding.cpp

# Drops MathUtil.cpp's parseNumber() (CLI-only, the sole reason that file
# pulled in boost::regex — a compiled Boost component this project has no
# other use for and does not cross-compile to wasm). Verified applying
# cleanly with both `patch -p1` and `git apply` against a pristine
# checkout before being committed — see this repo's patches/audiowaveform/.
COPY ./patches/audiowaveform/no-boost-regex.patch /root/no-boost-regex.patch
RUN cd /root/audiowaveform && git apply --no-index -v /root/no-boost-regex.patch

WORKDIR /root/audiowaveform

# Core file set confirmed by direct source inspection (CLAUDE.md decisions
# 8 and 13): no CLI (Main/Options/OptionHandler), no PNG rendering
# (GdImageRenderer/Rgba/WaveformColors), no CLI-only helpers
# (DurationCalculator/WavFileWriter/AudioLoader/FileFormat).
# FileHandle (needed directly by Mp3AudioFileReader) and, once
# SndFileAudioFileReader entered the picture, ProgressReporter/TimeUtil/
# Array.h (needed directly by SndFileAudioFileReader, not just CLI progress
# display the way they are for GdImageRenderer) turned out to be required
# too — see decision 13's correction to decision 8's original file list.
#
# Both madlld's bstdfile.c and pdjson.c are included from C++ through a
# header that self-wraps in `extern "C" { ... }` (BStdFile.h explicitly
# for bstdfile.h; pdjson.h guards itself with `#ifdef __cplusplus`), so
# callers expect plain C-linkage (unmangled) symbols from them. Confirmed
# on the first real build that this genuinely matters: em++ compiles every
# input as C++ regardless of extension (a "treating 'c' input as 'c++'"
# deprecation warning was the tell), which produced mangled symbols
# instead and made wasm-ld report `undefined symbol: NewBstdFile` and
# friends even though the object file did contain a (mangled) definition.
# A first attempt at fixing this with a positional `-x c` in one big
# em++ invocation backfired differently: em++'s Python driver compiles
# multiple sources by re-invoking clang++ once per file, but doesn't
# track `-x` positionally across that loop the way real clang does — it
# ended up applying `-x c` (and the resulting "not allowed with C" clash
# with `-std=c++11`) to unrelated .cpp files too. Fixed properly by
# compiling the two .c files in their own explicit `emcc -c` step first
# (real C, correct unmangled symbols), then linking those two .o files
# alongside the .cpp sources in the final em++ invocation — no ambiguity
# about which flags apply to which file.
RUN mkdir -p /root/out

RUN source /root/emsdk/emsdk_env.sh && \
    emcc -O2 -I src -I /root/lib/include -idirafter /usr/include \
        -c src/madlld-1.1p1/bstdfile.c -o /root/out/bstdfile.o && \
    emcc -O2 -I src -I /root/lib/include -idirafter /usr/include \
        -c src/pdjson/pdjson.c -o /root/out/pdjson.o

# -idirafter /usr/include (not -isystem, and not -I): confirmed on the
# first real build that emcc's cross-compiler sysroot doesn't see the host
# Linux system's headers by default (correct behavior in general), even
# though base-image/Dockerfile's `apt install libboost-dev` genuinely does
# put boost/format.hpp at /usr/include/boost/format.hpp (verified
# directly: `docker run ... find / -iname format.hpp` finds it). A first
# attempt with `-isystem /usr/include` overcorrected: `-isystem` still
# ranks ahead of some of emcc's own sysroot paths, so `<cassert>` ended up
# resolving to the HOST's real glibc `assert.h`, which then choked on
# `bits/wordsize.h` (a glibc-internal, multiarch-subdirectory header this
# flat include path doesn't reach) — exactly the "host-architecture-
# specific header getting picked up by a cross-compiler" risk the original
# comment worried about, now actually triggered for libc instead of boost.
# `-idirafter` is the standard fix for "add a fallback include path without
# letting it shadow the compiler's own headers": it's only ever consulted
# after every other search path (including emcc's own sysroot) has already
# failed to resolve an #include — so emcc's own libc headers still win,
# and only `boost/*` (which doesn't exist in emcc's sysroot at all) falls
# through to the host path.
RUN source /root/emsdk/emsdk_env.sh && \
    em++ \
        -O2 \
        -g \
        -std=c++11 \
        -I src \
        -I /root/lib/include \
        -idirafter /usr/include \
        -s ENVIRONMENT=node \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s MODULARIZE=1 \
        -s EXPORT_NAME=createAudiowaveformModule \
        -lembind \
        src/binding.cpp \
        src/AudioFileReader.cpp \
        src/AudioProcessor.cpp \
        src/BStdFile.cpp \
        src/Error.cpp \
        src/FileHandle.cpp \
        src/FileUtil.cpp \
        src/Log.cpp \
        src/MathUtil.cpp \
        src/Mp3AudioFileReader.cpp \
        src/ProgressReporter.cpp \
        src/SndFileAudioFileReader.cpp \
        src/TimeUtil.cpp \
        src/WaveformBuffer.cpp \
        src/WaveformGenerator.cpp \
        src/WaveformRescaler.cpp \
        src/WaveformUtil.cpp \
        /root/out/bstdfile.o \
        /root/out/pdjson.o \
        /root/lib/lib/libmad.a \
        /root/lib/lib/libid3tag.a \
        /root/lib/lib/libsndfile.a \
        -o /root/out/audiowaveform.js
