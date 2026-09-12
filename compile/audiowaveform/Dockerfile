# Final module: fetches audiowaveform's own source (unmodified — see
# CLAUDE.md decision 4/8, we extract, not fork), compiles only the
# confirmed-minimal core file set, links against libmad/libid3tag, and
# adds our own Embind wrapper (src/binding.cpp).
FROM kirigami-audiowaveform-wasm:base

ARG AUDIOWAVEFORM_VERSION

COPY --from=kirigami-audiowaveform-wasm:libmad /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libid3tag /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libsndfile /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:libfaad2 /root/lib /root/lib
COPY --from=kirigami-audiowaveform-wasm:nestegg /root/lib /root/lib

RUN wget "https://github.com/bbc/audiowaveform/archive/refs/tags/$AUDIOWAVEFORM_VERSION.tar.gz" -O audiowaveform.tar.gz && \
    tar xf audiowaveform.tar.gz && \
    mv audiowaveform-$AUDIOWAVEFORM_VERSION /root/audiowaveform

COPY ./compile/src/binding.cpp /root/audiowaveform/src/binding.cpp
COPY ./compile/src/M4aAudioFileReader.h ./compile/src/M4aAudioFileReader.cpp /root/audiowaveform/src/
COPY ./compile/src/WebmAudioFileReader.h ./compile/src/WebmAudioFileReader.cpp /root/audiowaveform/src/

# mp4read.c/.h + unicode_support.c/.h: libfaad2's own frontend MP4 demuxer
# (CLAUDE.md decision 9's correction #2) — not part of the installed
# libfaad.a, pulled directly from the libfaad2 build stage's source tree
# (which still has the full checkout at /root/faad2) rather than downloading
# a second time. Not audiowaveform's code either, same "vendor the specific
# files actually needed" approach as bstdfile.c/pdjson.c below.
COPY --from=kirigami-audiowaveform-wasm:libfaad2 \
    /root/faad2/frontend/mp4read.c \
    /root/faad2/frontend/mp4read.h \
    /root/faad2/frontend/unicode_support.c \
    /root/faad2/frontend/unicode_support.h \
    /root/audiowaveform/src/faad2-frontend/

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

# mp4read.c/unicode_support.c get the same "compile as real C, not C++"
# treatment as bstdfile.c/pdjson.c above, for the same reason: mp4read.h
# doesn't self-wrap in extern "C" the way BStdFile.h/pdjson.h do, so
# M4aAudioFileReader.cpp wraps its own #include "mp4read.h" in extern "C"
# instead (see that file) — but the symbols mp4read.o itself exports still
# need to be real, unmangled C linkage to match that wrapped declaration.
RUN source /root/emsdk/emsdk_env.sh && \
    emcc -O2 -I src -I /root/lib/include -idirafter /usr/include \
        -c src/madlld-1.1p1/bstdfile.c -o /root/out/bstdfile.o && \
    emcc -O2 -I src -I /root/lib/include -idirafter /usr/include \
        -c src/pdjson/pdjson.c -o /root/out/pdjson.o && \
    emcc -O2 -I src/faad2-frontend -I /root/lib/include -idirafter /usr/include \
        -c src/faad2-frontend/mp4read.c -o /root/out/mp4read.o && \
    emcc -O2 -I src/faad2-frontend -I /root/lib/include -idirafter /usr/include \
        -c src/faad2-frontend/unicode_support.c -o /root/out/unicode_support.o

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
#
# -s STACK_SIZE=5MB: Emscripten's default (64KB) is too small for the call
# depth libc++'s chained ostream operator<< / sentry machinery needs —
# confirmed as the actual root cause (not a std::cout-specific bug, our
# first hypothesis — see src/binding.cpp's nullStream comment) of a real
# runtime crash on the first smoke test: WaveformGenerator::init()'s
# `log(Info) << "..." << someInt << ...` calls corrupted memory deep in
# iostream internals, manifesting as either "table index is out of
# bounds" or "memory access out of bounds" depending on build details —
# classic stack-overflow symptoms (varying crash site, not a consistent
# logic bug). 5MB is generous headroom, not a tuned minimum.
#
# -s EXPORT_ES6=1: without it, Emscripten's MODULARIZE output is a UMD-
# style file (a `module.exports = factory` branch, no real `export`
# syntax) — Node's CJS/ESM interop maps that to `import().default` only
# when the file itself is resolved as CommonJS. That's implicit and
# fragile: found the hard way when copying this output into
# @kirigami/audiowaveform-wasm (a real `"type": "module"` package) made
# Node treat the vendored dist/audiowaveform.js as ESM too (no controlling
# package.json says otherwise inside that package), so the UMD branch
# never ran and `import().default` came back `undefined`
# ("TypeError: createModule is not a function") — whereas the same file
# loaded fine from this repo's own node-builds/ (no package.json in the
# tree at all, so Node defaults to CJS). EXPORT_ES6 makes Emscripten emit
# a real `export default` instead, so it works correctly regardless of
# the consuming package's own module type — matches this project's ESM
# convention (decision 15) properly instead of relying on Node's
# extension-based guessing.
RUN source /root/emsdk/emsdk_env.sh && \
    em++ \
        -O2 \
        -std=c++11 \
        -I src \
        -I src/faad2-frontend \
        -I /root/lib/include \
        -idirafter /usr/include \
        -s ENVIRONMENT=node \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s STACK_SIZE=5MB \
        -s MODULARIZE=1 \
        -s EXPORT_ES6=1 \
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
        src/M4aAudioFileReader.cpp \
        src/WebmAudioFileReader.cpp \
        src/TimeUtil.cpp \
        src/WaveformBuffer.cpp \
        src/WaveformGenerator.cpp \
        src/WaveformRescaler.cpp \
        src/WaveformUtil.cpp \
        /root/out/bstdfile.o \
        /root/out/pdjson.o \
        /root/out/mp4read.o \
        /root/out/unicode_support.o \
        /root/lib/lib/libmad.a \
        /root/lib/lib/libid3tag.a \
        /root/lib/lib/libsndfile.a \
        /root/lib/lib/libFLAC.a \
        /root/lib/lib/libvorbisenc.a \
        /root/lib/lib/libvorbis.a \
        /root/lib/lib/libogg.a \
        /root/lib/lib/libopus.a \
        /root/lib/lib/libfaad.a \
        /root/lib/lib/libnestegg.a \
        /root/lib/lib/libz.a \
        -o /root/out/audiowaveform.js
# libz.a: confirmed necessary once binding.cpp started calling into
# libid3tag's real tag-reading API (id3_tag_findframe/id3_frame_field/...,
# getId3Tags()/getId3CoverArt()) rather than only the internal
# skip-past-the-tag path audiowaveform's own Mp3AudioFileReader uses —
# pulled in a different set of libid3tag.a's object files, one of which
# (util.o) references zlib's uncompress() (used for ID3v2's rarely-hit
# compressed-frame support). Already present at /root/lib/lib/libz.a
# (copied in transitively via the libid3tag build stage, which vendors it
# for the same reason — see compile/libid3tag/Dockerfile) — just wasn't
# on this link line until it was actually needed.
#
# libFLAC.a/libvorbisenc.a/libvorbis.a/libogg.a/libopus.a: FLAC/Ogg-Vorbis/
# Ogg-Opus support (CLAUDE.md decision 9's correction) — libsndfile.a only
# calls into these, doesn't re-export their symbols, so they still need to
# be on this final link line explicitly, same as libz.a above. All four are
# already merged into /root/lib by compile/libsndfile/Dockerfile's own
# COPY --from= chain, so no new COPY line is needed here — only these link
# args. libvorbisenc.a before libvorbis.a: matches libsndfile's own real
# CMakeLists.txt target_link_libraries ordering (Vorbis::vorbisenc listed
# before the plain Vorbis dependency it itself depends on).
#
# libfaad.a: M4A/AAC support (CLAUDE.md decision 9's correction #2) via
# src/M4aAudioFileReader.cpp (this project's own code, not audiowaveform's —
# see that file) plus the vendored mp4read.o/unicode_support.o demuxer
# object files built above.
#
# libnestegg.a: WebM audio support (decision 19) via
# src/WebmAudioFileReader.cpp (also this project's own code) — nestegg only
# demultiplexes the WebM/Matroska container, decoding reuses the
# libvorbis.a/libopus.a already on this link line above.
