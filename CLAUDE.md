# audiowaveform-wasm-compiler

## Context

Sibling project to `../php-wasm-compiler` in the Kirigami ecosystem. Goal:
compile [BBC's `audiowaveform`](https://github.com/bbc/audiowaveform)
(a C++ CLI tool that generates waveform peak data — and optionally PNG
renders — from audio files) to WebAssembly, with a Node.js programming
interface, to power a Kirigami **MP3 player plugin** (waveform display
under the seek bar, SoundCloud-style).

`audiowaveform` itself is GPL-3.0 (confirmed via `gh api
repos/bbc/audiowaveform`) and links `libmad` (MP3 decoding), which is
GPL-2.0 (confirmed via `gh api repos/markjeee/libmad`) — this repo inherits
GPL-3.0-or-later accordingly (see `LICENSE`), not the GPL-2.0-or-later
`php-wasm-compiler` uses (that one inherited its license from Playground,
an unrelated codebase).

## Decided architecture

Decisions settled with the user (2026-09-12):

1. **Node.js only, no browser target.** Same reasoning as
   `php-wasm-compiler` decision 7 (Kirigami runs on Node — `kiri`/
   `kiribuild`). Confirmed explicitly by the user when asked.
2. **WASM chosen deliberately over a native Node addon (N-API/node-gyp),
   despite being Node-only.** This was discussed explicitly: for pure
   synchronous CPU work with no async I/O, a native addon would likely be
   simpler and faster, but it needs prebuilds per OS/arch/Node-ABI
   (`prebuildify`-style CI matrix). WASM ships one artifact that runs
   everywhere Node runs, and this repo can reuse the Emscripten/Docker
   pipeline knowledge and structure already built for `php-wasm-compiler`
   (same base-image approach, same kind of Makefile-driven third-party-lib
   builds) rather than starting a new build toolchain from scratch. The
   user's own words: "on pourrait faire un outil wasm avec les librairies,
   pour pouvoir extraire les peaks et générer toute l'interface avec du
   javascript" — i.e., WASM only does peak extraction; all UI/rendering is
   plain JS.
3. **Much simpler build than `php-wasm-compiler`: no JSPI, no Asyncify, no
   dylink/side-modules.** Peak extraction is synchronous, CPU-bound, no
   host I/O to proxy — a plain monolithic Emscripten build
   (`-s ENVIRONMENT=node`) should suffice. None of `php-wasm-compiler`'s
   hardest problems (decision 32's `__stack_pointer`/`MAIN_MODULE` side-
   module issues, JSPI async plumbing) are expected to apply here.
4. **Extract the core, not the CLI.** `audiowaveform`'s `main.cpp` is a
   CLI wrapper around `boost::program_options`; the actual peak-generation
   logic (`WaveformGenerator`, `AudioFileReader` and its
   MP3/WAV/etc. subclasses) is what gets compiled — not the CLI argument
   parsing, and not `libgd`-based PNG rendering (rendering is JS's job per
   decision 2). **Not yet verified**: how much the core peak-generation
   code itself depends on boost beyond the CLI layer (e.g.
   `boost::filesystem`, smart pointers) — needs a real read of the
   `audiowaveform` source tree before assuming it can be excluded
   entirely.
5. **JS API via Embind, not a raw C `ccall`/`cwrap` ABI.** Structured
   data (peaks, options) should come back as real JS
   objects/typed arrays without hand-rolled malloc/free plumbing on the
   JS side.
6. **Output format should match `audiowaveform`'s own peaks format**
   (the `.dat`/JSON format its `-o` flag produces), specifically so BBC's
   own companion npm package,
   [`waveform-data.js`](https://github.com/bbc/waveform-data.js), can
   parse/render it directly — potentially avoiding writing any custom
   waveform-rendering JS at all, only the extraction side is this repo's
   job.
7. **Libraries needed, provisional**: `libmad` + `libid3tag` for MP3
   decoding (the plugin's primary use case, per decision on scope: "un
   plugin de lecteur mp3"). `libsndfile` (WAV/FLAC/etc.) is a maybe —
   not yet decided whether v1 needs more than MP3 support.
8. **Boost dependency scope confirmed by actually reading the source
   (2026-09-12) — resolves decision 4's open question: the extractable
   core needs zero *compiled* Boost libraries, only (optionally) one
   header-only one.** Cloned `bbc/audiowaveform` to a scratchpad and
   grepped every `boost::` usage against the real `add_executable(audiowaveform
   ${SRCS})` file list in `CMakeLists.txt`:
   - `boost::program_options` / `boost::filesystem` (the two heaviest,
     *compiled* Boost components CMake requires): confirmed used only in
     `Options.cpp`/`OptionHandler.cpp`/`Main.cpp` — pure CLI, entirely
     excluded by decision 4.
   - `boost::regex` (also compiled, not header-only): used in exactly two
     places, both excludable — `MathUtil::parseNumber` (only called from
     `Options.cpp`, CLI numeric-arg parsing) and `RGBA::parse` (only used
     by `GdImageRenderer.cpp`/`Options.cpp`/`WaveformColors.cpp`, all
     PNG-rendering/CLI-color-option code, decision 4's `libgd` exclusion).
     Neither the regex patterns nor the functions using them are reachable
     from the peak-generation path — **zero compiled Boost libraries need
     cross-compiling to wasm**, unlike every other Boost-touching part of
     this repo's cousin `php-wasm-builder` fork history.
   - `boost::format` (header-only, no link step) and `boost::to_lower_copy`
     (header-only `Boost.StringAlgo`): still appear in a few core-adjacent
     files (`Error.h`/`.cpp`, `WaveformBuffer.cpp`, `WaveformGenerator.cpp`
     use `boost::format` for exception messages; `FileFormat.cpp` uses
     `to_lower_copy` to normalize a CLI-supplied format string — that file
     itself is CLI-only and excluded). Decision: **keep `boost::format`
     as-is** in the extracted core rather than replacing it with
     `std::ostringstream` — since it's header-only, the Docker build only
     needs Boost's headers available (e.g. `apt install libboost-dev` in
     the base image), no separate `compile/libboost/Dockerfile` or
     cross-compiled `.a` the way every other third-party lib in this
     pipeline needs. Genuinely the cheapest possible Boost dependency.
   - **Concrete minimal core file set identified** (peak extraction only,
     no CLI, no PNG rendering), cross-checked against `CMakeLists.txt`'s
     real source list and each file's actual callers/callees:
     - `AudioFileReader.cpp/.h` (abstract base), `AudioProcessor.cpp/.h`
       (processor interface `WaveformGenerator` implements).
     - `Mp3AudioFileReader.cpp/.h` (needs `libmad` + `libid3tag`) +
       `BStdFile.cpp/.h` + `madlld-1.1p1/bstdfile.c` (the buffered-I/O
       adapter feeding libmad — confirmed actually compiled via
       `CMakeLists.txt`'s `MODULES` list, not just reference code sitting
       next to it).
     - `SndFileAudioFileReader.cpp/.h` (needs `libsndfile`) — only if v1
       supports non-MP3 input (decision 7, still open).
     - `VectorAudioFileReader.cpp/.h` (reads from an in-memory
       `std::vector<short>` instead of a file path — likely directly
       useful for a buffer-in JS API rather than something to route
       around).
     - `WaveformGenerator.cpp/.h` (the actual min/max peak-generation
       algorithm — the whole point of this project).
     - `WaveformBuffer.cpp/.h` + `pdjson/pdjson.c` (peaks storage +
       binary-`.dat`/JSON serialization via a small vendored JSON writer,
       license not yet double-checked — this is the file that directly
       produces the `waveform-data.js`-compatible format from decision 6).
     - `WaveformRescaler.cpp/.h` (rescales a peaks buffer to a different
       zoom level/resolution — a genuinely reusable core feature, not
       CLI-specific, worth exposing in the JS API too).
     - `WaveformUtil.cpp/.h` (amplitude-range helper, needs
       `MathUtil::scale`).
     - `MathUtil.cpp/.h`, **trimmed**: keep `scale`/`clamp`/
       `roundUpToNearest`/`roundDownToNearest`, drop `parseNumber` (and
       with it, the file's only `boost::regex` include) since it has
       exactly one caller and it's CLI-only.
     - `Error.cpp/.h` (throws using `boost::format`, kept per above), 
       `Log.cpp/.h` (trivial `iostream`-based logging, no heavy deps).
     - `FileHandle.cpp/.h`: **correction, found while writing the actual
       Embind wrapper (decision 12) — this one is required, not optional.**
       `Mp3AudioFileReader.h` itself `#include`s it and holds a `FileHandle
       file_` member; it's not just an output-side save/load helper the
       way this decision originally assumed. Trivial file either way (a
       plain RAII `FILE*` wrapper, no boost, no heavy deps) — just wrong
       to have called it skippable. `FileUtil.cpp/.h` remains genuinely
       optional (only relevant for file-based `.dat`/`.json` save/load,
       decision 10 skips that for v1).
   - **Confirmed excluded, with the concrete reason found in the source**:
     `DurationCalculator`, `FileFormat` (`boost::to_lower_copy`, CLI format
     string parsing), `GdImageRenderer` (`libgd`, PNG rendering),
     `Options`/`OptionHandler` (`boost::program_options`/`filesystem`, CLI
     orchestration — note `OptionHandler.cpp` is also where the *real*
     `createAudioFileReader()` → `WaveformGenerator` → `buffer.write(...)`
     pipeline is wired today; this repo's Embind wrapper needs to
     reimplement that wiring itself, not reuse `OptionHandler`),
     `ProgressReporter`/`TimeUtil` (CLI progress display), `Rgba`/
     `WaveformColors` (rendering colors), `WavFileWriter` (a CLI debug
     feature, dumps raw PCM to `.wav`), `AudioLoader` (only used by
     `OptionHandler`'s `--auto` amplitude-scaling special case — a
     legitimate feature, but not essential for v1; revisit as a v2 if the
     auto-scale behavior turns out to matter for the player UI).

9. **v1 format scope: MP3 (primary) + WAV/FLAC/Ogg/Opus via `libsndfile`
   (since it's already part of the extracted core's `AudioFileReader`
   family at no extra architectural cost); M4A/AAC deferred to v2
   (2026-09-12).** User asked whether M4A support was possible. Checked
   the actual source: `audiowaveform` has **no** M4A/AAC support anywhere
   — not in its own code, not via `libsndfile` (which covers WAV/AIFF/
   FLAC/Ogg-Vorbis/Opus, but not the MP4 container or the AAC codec). This
   isn't "flip a flag," it's a genuinely new decoder path. Two realistic
   options identified: **FFmpeg** (`libavformat`+`libavcodec` — decodes
   everything, including AAC/MP4, but is by far the heaviest possible
   dependency to cross-compile here, a project-sized undertaking on its
   own — see `ffmpeg.wasm` for prior art that it's *possible*, not that
   it's *cheap*) vs. **`libfdk-aac`** (a standalone AAC decoder) **+ a
   small single-header MP4 demuxer** (e.g. `minimp4.h`-style, just to pull
   the raw AAC elementary stream out of the `.m4a` box structure) — much
   more in line with this pipeline's "one targeted lib at a time" style
   (`libssh2`, `nghttp2`, `libsodium` in the `php-wasm-compiler` cousin
   project were all added this same way). **Decision: defer M4A to v2**,
   but keep `AudioFileReader` as the extension point (decision 8's file
   list already treats it as an abstract base with per-format
   subclasses) so adding `M4aAudioFileReader` later is additive, not a
   redesign. Not yet started: no `libfdk-aac`/MP4-demuxer research beyond
   naming the two candidate approaches.
   **Correction (2026-09-12, while actually wiring up `libsndfile`,
   decision 13):** "at no extra architectural cost" for FLAC/Ogg/Opus was
   wrong. `libsndfile` only reads those formats when built against
   `libFLAC`/`libogg`/`libvorbis`/`libopus`, none of which this project
   vendors. This project's `libsndfile` build uses
   `ENABLE_EXTERNAL_LIBS=OFF` — WAV/AIFF/RAW only for now. FLAC/Ogg/Opus
   get the same "add the extra lib later" deferral as M4A, not a free ride.
   **Correction #2 (2026-09-12): `libfdk-aac` is very likely GPL-incompatible
   — checked its actual license text, not just its reputation.** Fetched
   `mstorsjo/fdk-aac`'s `NOTICE` file directly: "Software License for The
   Fraunhofer FDK AAC Codec Library," section 3, verbatim: **"NO EXPRESS OR
   IMPLIED LICENSES TO ANY PATENT CLAIMS... ARE GRANTED BY THIS SOFTWARE
   LICENSE."** GitHub itself can't classify it as a standard SPDX license
   (`NOASSERTION`/"Other"). This is exactly why Debian/Fedora exclude
   `libfdk-aac` from their main repositories — a license that explicitly
   disclaims any patent grant doesn't satisfy GPLv3's own explicit patent-
   grant requirements (and GPLv2 is built on the same "no additional
   restrictions" premise), so combining it into a work distributed under
   this project's `GPL-3.0-or-later` is a real problem, not a formality.
   **Found a better-fitting alternative: `libfaad2`** (`knik0/faad2` on
   GitHub, actively maintained — last push 2026-08-27). Checked its actual
   source headers rather than trusting reputation here either:
   `libfaad/decoder.c`'s own license header reads "either version 2 of the
   License, or (at your option) any later version" — genuine
   **GPL-2.0-or-later**, compatible with this project's GPL-3.0-or-later
   (unlike a strict GPL-2.0-only component would be). Bonus: `faad2`'s own
   `frontend/mp4read.c` (a real, ~33KB, self-contained MP4 box demuxer —
   not a stub) is separately headed **GPL-3.0-or-later** (2017, current
   maintainer Krzysztof Nikiel) — an exact license match with this project,
   and it eliminates the need for a separate third-party `minimp4.h`-style
   demuxer entirely: both the AAC decoder and the MP4 demuxer can come from
   the same upstream project, same license family. Revised M4A plan:
   **`libfaad2` + its own bundled `mp4read.c`**, not `libfdk-aac` + a
   separate demuxer. One caveat carried over honestly, not solved by this
   correction: `libfaad2`'s own license text still says "Any non-GPL usage
   of this software or parts of this software is strictly forbidden," and
   AAC itself remains patent-encumbered technology regardless of which
   implementation decodes it (this is a real-world patent-licensing
   question independent of copyright license text, and applies equally to
   `libfdk-aac`, `libfaad2`, or FFmpeg's own AAC decoder) — not something a
   choice of decoder library resolves, just something this correction
   doesn't make worse and doesn't claim to fix. M4A/AAC work itself is
   still deferred to v2 (this is research, not implementation) — no
   `compile/libfaad2/Dockerfile` written yet.
10. **JS API will be buffer-in/buffer-out only for v1 — no file-based
    `.dat`/`.json` save/load.** Resolves decision 8's last open bullet:
    skip `WaveformBuffer`'s file-based `save()`/`saveAsJson()`/`load()`
    methods, using its plain in-memory accessors instead (decision 12's
    `binding.cpp`). Simpler, and fits a Node API better than routing
    through the wasm virtual filesystem; revisit if a file-based mode
    turns out to be genuinely useful later. (`FileHandle.cpp`/`FileUtil.cpp`
    turned out to be required in the core anyway, for unrelated reasons —
    see decisions 8 and 13's corrections — so this decision is narrower
    than it originally sounded.)
11. **Deliberately not running any `docker build`/`make` yet, even though
    the Dockerfiles/Makefile below got written (2026-09-12).** Checked
    free disk space on `C:` before starting this work:  only **8.6GB
    free** — the exact danger zone `php-wasm-compiler`'s own CLAUDE.md
    documents ("below a couple GB free, Windows itself starts failing in
    confusing ways") — and `php-wasm-compiler`'s own PHP build is running
    concurrently on this same machine, presumably itself consuming
    disk/RAM. Starting a second heavy Emscripten/Docker build right now
    (pulling `ubuntu:noble` + emsdk again, compiling `libmad`/`libid3tag`)
    risks reproducing tonight's earlier disk-space crisis (see
    `php-wasm-compiler/CLAUDE.md`'s Environment section) across *two*
    concurrent builds instead of one. So: author the build pipeline now
    (zero disk/RAM cost), actually run it once the PHP build finishes and
    disk headroom is confirmed again.
12. **Full build pipeline authored (not yet run) while blocked on disk
    space by decision 11 (2026-09-12): `compile/base-image/Dockerfile`,
    `compile/libmad/Dockerfile`, `compile/libid3tag/Dockerfile`,
    `compile/audiowaveform/Dockerfile`, `compile/Makefile`, and the actual
    Embind wrapper `compile/src/binding.cpp`.**
    - Deliberately **not** the full `config.yaml`/`matrix.json`/`cli.mjs`
      apparatus `php-wasm-compiler` has — that machinery earns its keep
      there because of a real matrix (dozens of extensions × PHP
      versions). This project has two fixed-version, dead-upstream libs
      (`libmad`/`libid3tag`, both pinned `0.15.1b` — confirmed via
      `gh api repos/markjeee/libmad` that no real GitHub tags exist to
      track "latest" against, unlike `audiowaveform` itself, which does
      have real tags, latest `1.10.3` per `gh api
      repos/bbc/audiowaveform/tags`, pinned in the Makefile) and one
      purpose-built final module — that whole apparatus would be
      premature abstraction here. Revisit if the lib/format count grows
      enough to justify it (e.g. once M4A's `libfaad2` and `libsndfile`
      both get added).
    - `base-image/Dockerfile`: `php-wasm-compiler`'s base image trimmed of
      everything JSPI/side-module-specific (no `emcc-for-php-wasm.sh`
      patching — decision 3, this is a plain monolithic module, not a
      dylink `MAIN_MODULE`), same Emscripten `4.0.19` pin (no strong
      reason to diverge), plus `libboost-dev` (decision 8's header-only
      `boost::format`).
    - `libmad/Dockerfile` / `libid3tag/Dockerfile`: standard
      `emconfigure`/`emmake` autotools cross-compile, `--host
      wasm32-unknown-emscripten`, sourced from the SourceForge "mad"
      project (the actual canonical, if long-dead, upstream). Two
      concrete unverified risk areas flagged **as comments in the
      Dockerfiles themselves**, since neither can be checked without an
      actual build (decision 11): (a) libmad's `fixed.h` selects a
      fixed-point math implementation partly via hand-written per-
      architecture inline assembly (`FPM_INTEL`/`FPM_ARM`/etc.) — this
      project does *not* pass a `-D__x86_64__`-style arch-spoofing flag
      (unlike `php-wasm-compiler`'s SIMD-intrinsics libs, see that repo's
      decision 32 for why that flag exists there), so plain autoconf
      host-triplet detection should fall through to the portable
      `FPM_DEFAULT` path — confirm this is actually what gets selected at
      the first real build; (b) libid3tag optionally links zlib for
      compressed-ID3v2-frame support, and this project doesn't vendor
      `libz` yet — confirm whether that's a soft (skippable) or hard
      configure dependency before assuming the build succeeds unmodified.
    - `audiowaveform/Dockerfile`: downloads `audiowaveform`'s real GitHub
      release tarball fresh (not a committed vendored copy — same
      "download at build time, patch via a real `.patch` file" pattern
      `php-wasm-compiler` settled on for `cmark`, decision 34, not the
      "vendor + hand-edit" false start that decision made and reverted for
      the same reason), applies `patches/audiowaveform/no-boost-regex.patch`
      via `git apply --no-index` (exact same invocation
      `php-wasm-compiler/compile/php/Dockerfile` uses), then compiles the
      confirmed core file list (decision 8, corrected) plus
      `src/binding.cpp` with `em++ -lembind`, linked against the
      `libmad`/`libid3tag` `.a` files staged in via `COPY --from=`
      referencing those two images by tag (built separately by the
      Makefile, not a Dockerfile multi-stage `FROM`).
    - `patches/audiowaveform/no-boost-regex.patch`: a **real, generated
      and verified** patch (not just described) — removes `MathUtil.cpp`'s
      `parseNumber()` and its `#include <boost/regex.hpp>` (decision 8's
      "trimmed" `MathUtil.cpp` was previously just a stated intent; this
      is the actual mechanism). Verified applying cleanly with both
      `patch -p1 --dry-run` and `git apply --check` against a pristine
      checkout of `audiowaveform`'s real `MathUtil.cpp` (cloned to a
      scratchpad for this purpose) before being committed — same
      verification discipline `php-wasm-compiler` decision 34 used for its
      `cmark` patch. One real snag hit and fixed while generating it: the
      first diff attempt failed to apply (`patch`) / warned (`git apply`)
      due to CRLF line endings introduced by this being authored on
      Windows against a `git clone` that itself checked out CRLF (this
      machine's `core.autocrlf`) — fixed by normalizing both sides to LF
      with `dos2unix` before diffing, since the real build target (a
      fresh download inside a Linux container) will have native LF.
    - `compile/src/binding.cpp`: exposes `extractMp3Peaks(bytes,
      samplesPerPixel) -> object` via Embind. Written directly from
      reading `AudioFileReader.h`/`Mp3AudioFileReader.h`/
      `WaveformGenerator.h`/`WaveformBuffer.h`'s real interfaces (not
      guessed), reimplementing the shape of
      `OptionHandler::generateWaveformData()`'s pipeline
      (`open()` → `WaveformGenerator` as `AudioProcessor` → `run()`) without
      any of `OptionHandler`'s CLI machinery. Key design point: every
      `AudioFileReader` subclass in `audiowaveform` reads from a real file
      path, not a memory buffer, so the incoming JS `Uint8Array` gets
      staged into Emscripten's default in-memory MEMFS at a fixed temp
      path before calling `Mp3AudioFileReader::open()` unmodified —
      keeps this wrapper a thin adapter rather than requiring changes to
      `audiowaveform`'s own reader code. Returns a plain JS object shaped
      like `audiowaveform`'s own documented JSON peaks format
      (`version`/`channels`/`sample_rate`/`samples_per_pixel`/`bits`/
      `length`/`data`) rather than a serialized string, so callers get
      real typed data without a `JSON.parse()` round-trip; `JSON.stringify()`
      on the result reconstructs the literal file format.
    - **None of this has been compiled yet** (decision 11) — every risk
      area called out above is a documented hypothesis to verify at the
      first real `make`/`docker build`, not a confirmed working build the
      way `php-wasm-compiler`'s equivalent decisions are.

13. **`libsndfile` (WAV/AIFF/RAW) wired in, and a real correction to
    decision 8's core file list found while doing it (2026-09-12).** Before
    writing `compile/libsndfile/Dockerfile`, read `SndFileAudioFileReader.h`/
    `.cpp` directly rather than assuming it was as self-contained as
    `Mp3AudioFileReader` — good thing, because it isn't:
    `SndFileAudioFileReader.cpp` unconditionally instantiates a
    `ProgressReporter` inside `run()` and calls `FileUtil::isStdioFilename()`/
    `FileUtil::getInputFilename()` inside `open()`. Decision 8 had marked
    `ProgressReporter`/`TimeUtil` "confirmed excluded, CLI-only" — true for
    the MP3-only path (`Mp3AudioFileReader` never touches either), false
    once `SndFileAudioFileReader` is in the link set: `ProgressReporter.cpp`
    itself pulls in `Array.h` (header-only, no `.cpp`) and `TimeUtil.cpp`.
    All confirmed lightweight on inspection (`FileUtil.cpp`: `<fstream>`/
    `<sys/stat.h>`, no boost; `TimeUtil.cpp`: just `<cstdio>`) — no new
    cross-compiled dependency, just more files in the `em++` command.
    Corrected `compile/audiowaveform/Dockerfile`'s file list and its
    explanatory comment accordingly; `compile/src/binding.cpp` gained
    `extractWavPeaks()` alongside `extractMp3Peaks()`, sharing a common
    `extractPeaks(AudioFileReader&, bytes, samplesPerPixel)` helper (both
    readers get staged into MEMFS the same way — libsndfile auto-detects
    format from the file header, not a filename extension, so one fixed
    temp path serves both).
    - `compile/libsndfile/Dockerfile`: CMake-based (libsndfile 1.1+ dropped
      autotools), `emcmake`/`emmake`, `ENABLE_EXTERNAL_LIBS=OFF` (see
      decision 9's correction above — no FLAC/Ogg/Opus yet) and
      `ENABLE_MPEG=OFF` (libsndfile can optionally read/write MP3 via
      `mpg123`/`lame`; deliberately not enabled since this project already
      has a real MP3 path via `libmad`/`libid3tag` and vendoring a second,
      redundant MP3 codec stack would be pure waste).
    - Also not yet compiled/tested — same status as the rest of decision 12.

14. **Adopted `php-wasm-compiler`'s `matrix.json`/version-tracking
    mechanism after all, reversing part of decision 12 (2026-09-12,
    explicit user request: "on va tracker les versions des dépendances et
    du projet audiowaveform de bbc en tant que tel").** Decision 12 argued
    the full `config.yaml`/`matrix.json`/`cli.mjs` apparatus was premature
    for two fixed-version libraries — still true for the CLI/config-file
    part, but the user specifically wants the *version-tracking* half
    (`matrix.json` + a "check for latest" script) regardless of how few
    libraries exist today, applied to `audiowaveform` itself, not only its
    C dependencies. Added:
    - `matrix.json` (repo root): a `libraries` map covering `audiowaveform`
      itself (`sourceType: "github-release"`, repo `bbc/audiowaveform`,
      real tags, latest `1.10.3`), `libmad`/`libid3tag` (`sourceType:
      "tarball"`, fixed `0.15.1b` pins — explicitly documented as having no
      real "latest" to track, unlike the others, per decision 12's
      research), and `libsndfile` (`sourceType: "github-release"`, latest
      `1.2.2`). Treating `audiowaveform` as just another `libraries` entry
      (rather than inventing a separate top-level key for "the tool itself
      vs. its dependencies") was a deliberate simplicity choice — it lets
      `update-lib-versions.mjs` handle it with zero special-casing.
    - `compile/matrix-version.mjs` and `compile/update-lib-versions.mjs`:
      ported from `php-wasm-compiler` (same mechanism, `getMatrixVersion()`
      reads a library's `versions[]` last entry; the updater queries GitHub
      releases/tags for `github-release`/`github-tag` sourceTypes and
      appends newly-found versions). **Both actually run and verified
      working** (pure network + local file I/O, no Docker/disk risk, safe
      to run even under decision 11's constraint):
      `node compile/update-lib-versions.mjs` correctly reported everything
      already up to date, and `node compile/matrix-version.mjs <key>`
      correctly resolved all four entries.
    - `compile/Makefile`: `LIBMAD_VERSION`/`LIBID3TAG_VERSION`/
      `LIBSNDFILE_VERSION`/`AUDIOWAVEFORM_VERSION` now all resolve via a
      `MATRIX_VERSION` macro (identical mechanism to
      `php-wasm-compiler/compile/Makefile`'s own) instead of hardcoded
      literals. Verified with `make -n` (dry-run) that every target
      resolves the exact same versions matrix.json records, without
      needing an actual build.
    - Left alone deliberately: `config.yaml`/`cli.mjs` (decision 12's other
      half) — the user's request was specifically about version tracking,
      not the interactive-CLI/config-file layer. Revisit that separately if
      asked.

15. **ESM throughout, like every other Kirigami project (2026-09-12,
    explicit user note).** `compile/matrix-version.mjs` and
    `compile/update-lib-versions.mjs` were already ESM by construction
    (`.mjs`, `import`/`export`), but nothing had formalized this as a
    project-wide convention yet. Added `compile/package.json`
    (`"type": "module"`, mirroring `php-wasm-compiler/compile/package.json`'s
    shape) and fixed `README.md`'s Usage example, which had used
    CommonJS `require()` — corrected to `import`, matching the real pattern
    `../kirigami/packages/php-wasm/index.js` uses for loading its own
    Emscripten `MODULARIZE` output (`await import('./jspi/php_8_5.js')` —
    Node's ESM loader interops with Emscripten's default CJS-shaped
    output directly, no `-s EXPORT_ES6` build flag needed on the emcc
    side).

16. **First real end-to-end build succeeded, extensively runtime-tested
    against real audio and real tags, and the API surface evolved based on
    what that testing found (2026-09-12).** User said "Allons-y pour
    docker" once disk space recovered (a cleanup brought `C:` from 6.6GB
    to 26.4GB free); `make base-image` → `libmad` → `libid3tag` →
    `libsndfile` → `audiowaveform-wasm` all eventually succeeded, each
    hitting a real bug fixed in its own commit rather than guessed at:
    - **`.gitattributes` added**: this machine's system-wide
      `core.autocrlf=true` (Git for Windows default) was converting
      checked-out Dockerfiles to CRLF, corrupting bash heredocs (a literal
      `\r` landed inside a `git clone <url>` string, "Malformed input to a
      URL function"). Forced `* text=auto eol=lf` and re-checked-out every
      file.
    - **libmad/libid3tag**: bundled 2004-era `config.sub` didn't recognize
      `wasm32-unknown-emscripten` — fixed with a real, verified patch
      (`patches/libmad,libid3tag/config.sub-wasm32-emscripten.patch`,
      replacing it with a modern 2022 upstream copy), per the user's
      explicit instruction mid-session to patch rather than silently
      overwrite vendored files. Separately, both projects' `configure.ac`
      has a hand-written arg-parsing loop that traps a bare `-O2` (or
      autoconf's own unset-`CFLAGS` "-g -O2" default) into a set of
      GCC-specific 1990s optimization flags clang/emcc rejects outright —
      fixed with `CFLAGS="-DNDEBUG"` (deliberately not `-O2`, which
      re-triggers the exact same trap — an early attempt at this exact fix
      backfired for that reason). FPM_DEFAULT (portable fixed-point math,
      not x86/ARM asm) confirmed correctly selected, resolving decision
      7's original open risk.
    - **libid3tag also needed `libz` vendored** (`compile/libz/Dockerfile`,
      plain `emconfigure`/`emmake`, no JSPI machinery needed): its
      `configure` hard-requires `zlib.h` with no `--without-zlib` escape
      hatch. Not needed at first (the internal skip-past-the-tag path
      never touched it) — became necessary once `getId3Tags()`/
      `getId3CoverArt()` (below) pulled in different object files whose
      `util.o` calls `uncompress()`.
    - **Final `em++` link, several real bugs in sequence**: `boost/
      format.hpp` not found (emcc's cross sysroot doesn't see host
      headers by default, even though `libboost-dev` genuinely installs
      it) — fixed with `-idirafter /usr/include`, not `-isystem` (a first
      attempt with `-isystem` overcorrected and made `<cassert>` resolve
      to the HOST's real glibc `assert.h`, which then failed on a
      multiarch-subdirectory header); `madlld`'s `bstdfile.c`/`pdjson.c`
      compiled as C++ by `em++` regardless of extension, producing mangled
      symbols their own `extern "C"`-wrapping headers didn't expect
      (`undefined symbol: NewBstdFile`) — fixed by compiling those two
      files separately with real `emcc -c` first (a same-invocation
      positional `-x c` was tried first and made em++'s per-file clang++
      re-invocation apply `-x c` to unrelated `.cpp` files too);
      `output_stream`/`error_stream` (declared but only ever defined in
      the excluded `Main.cpp`) needed real definitions in `binding.cpp`;
      `/root/out` didn't exist yet for `-o`.
    - **A genuine runtime crash, not a build failure — the most
      significant find of the session**: the first successful build
      crashed the instant `WaveformGenerator::init()` wrote its first
      line (`RuntimeError: table index is out of bounds`, later `memory
      access out of bounds` after an unrelated change — the varying crash
      site was itself a clue). Root-caused with a temporary `-g` debug
      build for real symbol names, not guessed: Emscripten's **default
      wasm stack is 64KB**, too small for libc++'s chained `ostream
      operator<<`/`sentry` call depth. Fixed with `-s STACK_SIZE=5MB`.
      `output_stream`/`error_stream` were also redirected to a null
      `std::ostream` (not `std::cout`/`std::cerr`) — not load-bearing for
      the crash once the stack was fixed, kept anyway as a deliberate
      design choice (a library shouldn't silently print CLI-style text to
      the embedding process's real stdout).
    - **Real, working artifact produced and verified**: `node-builds/
      audiowaveform.wasm` (~640KB) + `audiowaveform.js`. Smoke-tested with
      real audio, not just "the build exited 0": `audiowaveform`'s own
      bundled `test/data/*.mp3`/`.wav` fixtures first, then a proper
      **format support matrix** — a real FLAC album (user-provided,
      `assets/`, gitignored) re-encoded via `ffmpeg` to WAV (16/24-bit
      PCM, 32-bit float), AIFF, MP3, Ogg Vorbis, Opus, and M4A/AAC.
    - **`extractMp3Peaks`/`extractWavPeaks` unified into one
      `extractAudioPeaks()`**, per the user's request, so callers don't
      need to know the format up front. **Two real false-positive bugs
      found and fixed in sequence while doing this**, both caught by
      *rerunning the format matrix test* immediately after each change
      rather than assuming the rename was safe: (1) a naive
      try-MP3-then-fall-back-on-failure dispatch let FLAC/Ogg/Opus/M4A
      "succeed" through `Mp3AudioFileReader` with a bogus 0Hz/zero-length
      result, because libmad's frame-sync scanner can find a handful of
      coincidentally-valid-looking sync words in essentially any
      high-entropy binary blob without treating that as a decode error;
      (2) a `buffer.getSize() == 0` heuristic (the first fix attempt)
      still weren't sufficient — some inputs decoded a handful of real
      garbage points, not exactly zero. **Fixed properly** with
      `detectFormat()`: sniff real container magic bytes (`RIFF`/`FORM` →
      libsndfile; `fLaC`/`OggS`/`....ftyp` → known-unsupported, fail
      immediately; anything else → try `Mp3AudioFileReader`) instead of
      any decode-attempt-based heuristic. Confirmed via the format matrix
      afterward: MP3/WAV/AIFF all pass with correct point counts,
      FLAC/Ogg/Opus/M4A all cleanly return `null`, no false positives left.
    - **`getId3Tags()`/`getId3CoverArt()` added**, per the user's request
      once `libid3tag` was confirmed vendored — `audiowaveform` itself
      only ever uses `libid3tag` internally to skip past tags before
      decoding, never to read values, so this is new code using
      `libid3tag`'s own public API directly (`id3_file_open`,
      `id3_tag_findframe`, `id3_frame_field`, `id3_field_getstrings`/
      `getlatin1`/`getbinarydata`, `id3_ucs4_utf8duplicate`). Frame field
      layouts (text-info frames' field 1 = STRINGLIST; `APIC`'s field
      1/2/4 = MIME/picture-type/binary data) confirmed by reading
      `libid3tag`'s own `frametype.c`, not guessed. Cover art returned as
      a plain byte-value array rather than a `typed_memory_view`, since
      the underlying bytes don't outlive `id3_file_close()`.
    - **User raised a real concern before testing**: `libid3tag` is a
      dead, 2004-era library — could it handle modern ID3v2.4/UTF-16
      correctly? Rather than swap to a heavier library (TagLib) on
      spec, tested empirically first, per this project's whole
      methodology. Result, genuinely reassuring: **25 real, randomly-
      sampled tagged MP3s** from the user's own library (title/artist/
      album/year/track/genre/cover art) — 0 failures, 0 crashes, correct
      UTF-8 output including French accents (Angélique, Fête). Then a
      **second, deliberately adversarial batch** generated with `ffmpeg`:
      explicit ID3v2.3 vs. ID3v2.4 (both parse, `TYER`/`TDRC` fallback
      confirmed working), ID3v1-only (no v2 at all — confirmed
      `id3_file_tag()` genuinely handles both, including resolving the
      numeric ID3v1 genre byte to text), a very long title (no
      truncation), a numeric-style `TCON` genre (`"17"`, not resolved to
      a name — a known, minor, undocumented-until-now limitation, not a
      crash), and — the real test of the user's UTF-16 concern — a title
      mixing Japanese, an emoji, French accents, and Cyrillic in one
      string, which came through byte-for-byte correct. **Conclusion:
      `libid3tag` is good enough as-is; no need to vendor a heavier tag
      library for now.**

17. **`@kirigami/audiowaveform-wasm` assembled and published to npm as
    `1.0.0` (2026-09-12).** Following the real, existing precedent — not
    guessed: `@kirigami/php-wasm` (the analogous PHP package) lives inside
    the **`kirigami` monorepo** at `kirigami/packages/php-wasm/` (an npm
    workspace, `"workspaces": ["packages/*"]`), published via that repo's
    own interactive `scripts/publish.js`, NOT assembled inside
    `php-wasm-compiler` itself. Confirmed this by actually reading that
    structure and `publish.js` before creating anything. Mirrored exactly
    for this project: `kirigami/packages/audiowaveform-wasm/` (a sibling
    repo to this one — `audiowaveform-wasm-compiler` — not a subdirectory
    of it), containing `package.json`, `index.js` (thin async wrapper —
    `extractAudioPeaks`/`getId3Tags`/`getId3CoverArt`, lazily instantiating
    and memoizing the wasm module), `index.d.ts` (full JSDoc'd types),
    `README.md` (Kirigami convention), `LICENSE` (GPL-3.0, copied from
    this repo), and `dist/audiowaveform.{js,wasm}` (the compiled output,
    copied in — not built from that repo).
    - **A real bug found while assembling the package, not before**:
      copying `node-builds/audiowaveform.js` as-is into the new package
      and testing it there threw `TypeError: createModule is not a
      function`. Root cause: Emscripten's `MODULARIZE` output without
      `EXPORT_ES6` is UMD-shaped (a `module.exports = factory` branch, no
      real `export` syntax) — Node's CJS/ESM interop only maps that to
      `import().default` when the file is *resolved* as CommonJS, which
      depends on the nearest controlling `package.json`'s `"type"` field.
      It worked by accident when tested straight from this repo's own
      `node-builds/` (no `package.json` anywhere in that directory tree,
      so Node defaults `.js` to CJS) but broke inside
      `@kirigami/audiowaveform-wasm`, a real `"type": "module"` package —
      Node treated the vendored file as ESM too, the UMD branch never
      ran, and `.default` came back `undefined`. **Fixed at the source**,
      not with a workaround in the consuming package: added
      `-s EXPORT_ES6=1` to `compile/audiowaveform/Dockerfile`'s `em++`
      invocation, so the compiler itself emits a genuine
      `export default createAudiowaveformModule;` — confirmed by
      inspecting the rebuilt file's actual tail. Works correctly
      regardless of the consumer's own module type now, which is the
      right fix given this project's own ESM convention (decision 15).
    - **Published for real**, not just assembled: `npm login` (the user,
      interactively, since Claude Code's own auto-mode classifier and the
      non-interactive Bash tool can't complete an npm web/2FA login flow)
      then `node scripts/publish.js --only audiowaveform-wasm`. First
      attempt with `--yes` alone hit `EOTP` (one-time-password required)
      — confirmed the auth URL npm prints is deliberately redacted
      (`***`) when npm's own output detects it's not a live interactive
      TTY, specifically to avoid a captured/piped consumer (this session
      included) acquiring a login session token; the correct path is
      either the user completing that browser flow directly themselves,
      or supplying a real TOTP code from their own authenticator app via
      `--otp`, which `publish.js` already supports as documented. User
      provided a live OTP code; republishing with `--otp <code>`
      succeeded. **Verified live on the real registry** afterward (not
      just trusted the script's own success message): `npm view` and a
      direct `curl` against `registry.npmjs.org` both initially 404'd —
      genuine publish-to-registry propagation delay for a brand-new
      package (confirmed via a polling loop, not assumed away), not a
      failed publish or an `npm view` local-cache artifact (checked both
      explanations before concluding it was just propagation lag) —
      resolved within about a minute.
    - Local `git commit` in the `kirigami` repo was scoped carefully:
      that repo already had unrelated uncommitted changes in progress
      (`packages/kirigami/bin/kiri.js`, `packages/php-wasm/jspi/...`) —
      staged and committed only the new `packages/audiowaveform-wasm/`
      paths explicitly, never a broad `git add -A`, so as not to sweep up
      or interfere with that other in-flight work. Pushed once the user
      confirmed.
    - **`@kirigami/audiowaveform-wasm@1.0.0` is now a real, installable
      npm package** — `npm install @kirigami/audiowaveform-wasm` works for
      anyone. This resolves the "published npm package name is not yet
      decided" item that had been open since this repo's very first
      scaffolding commit.

18. **FLAC/Ogg Vorbis/Opus and M4A/AAC all implemented, built, and verified
    working (2026-09-12) — resolves the format matrix's four remaining
    `❌ Not yet` rows (decisions 9/13/16).** Two independent pieces of work,
    both fully real (built + runtime-tested against real audio), not just
    written:

    - **FLAC/Ogg Vorbis/Opus**, via `libsndfile` rebuilt with
      `ENABLE_EXTERNAL_LIBS=ON` against four newly-vendored libs
      (`compile/libogg`, `compile/libflac` — `WITH_OGG=OFF`, native FLAC
      only, no Ogg-FLAC — `compile/libvorbis`, `compile/libopus`), reusing
      the existing `SndFileAudioFileReader` — no new C++ needed here.
      Licenses verified directly (not assumed), same discipline as
      `libfaad2`'s research in decision 9's correction #2: `xiph/ogg`,
      `xiph/flac`, `xiph/vorbis`, `xiph/opus` all ship the same Xiph
      3-clause-BSD-style `COPYING`, permissive and GPL-3.0-or-later
      compatible; Opus's own `COPYING` additionally documents its
      royalty-free IETF patent licenses (Xiph/Microsoft/Broadcom) — no
      patent ambiguity to carry forward the way AAC has.
      - **Real build bug #1**: `libopus`'s `configure` unconditionally
        probes x86 SSE/SSE2/SSE4.1/AVX2 intrinsics regardless of `--host`,
        and fails outright under `emconfigure` (`no supported Get CPU Info
        method`) since none of those compile under `emcc`/clang for
        wasm32 — it does not gracefully fall through to a portable path on
        its own. Fixed with the three flags `configure --help` itself
        documents for this: `--disable-rtcd --disable-intrinsics
        --disable-asm`.
      - **Real build/runtime bug #2, the significant one**: with
        `ENABLE_EXTERNAL_LIBS=ON` alone, every build step (each lib, then
        `libsndfile`, then the final `audiowaveform-wasm` link) succeeded
        with zero errors — but real FLAC/Ogg files still failed at
        *runtime* with libsndfile's own "File contains data in an
        unimplemented format." Root-caused with a temporary debug build
        (`binding.cpp`'s `output_stream`/`error_stream` pointed at
        `std::cerr` instead of the null stream) plus inspecting the built
        image's own `CMakeCache.txt`/`config.h` directly: `pkg_check_modules`
        correctly found `flac`/`ogg`/`vorbis`/`opus` (`PC_FLAC_FOUND=1`
        etc. — the base image's `PKG_CONFIG_PATH` works fine), but the
        subsequent `find_library`/`find_path` calls in libsndfile's own
        `cmake/FindFLAC.cmake` etc. still came back `NOTFOUND`, because
        Emscripten's own toolchain file sets
        `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY`/`INCLUDE=ONLY` — which
        restricts `find_library`/`find_path`, *even calls given explicit
        HINTS from a successful pkg-config lookup*, to searching only
        inside `CMAKE_FIND_ROOT_PATH` (the Emscripten sysroot), not
        `/root/lib` where every vendored lib in this project actually
        lives. Net effect: `config.h`'s `HAVE_EXTERNAL_XIPH_LIBS` silently
        resolved to `0` (libsndfile's own CMakeLists.txt has a quiet
        fallback: `if (ENABLE_EXTERNAL_LIBS AND NOT (Vorbis_FOUND OR
        FLAC_FOUND OR OPUS_FOUND)) set (ENABLE_EXTERNAL_LIBS OFF)`) even
        though `flac.c`/`ogg_vorbis.c`/`ogg_opus.c` still compiled cleanly
        as inert stubs — which is exactly why the *build itself* looked
        completely fine. Fixed with the standard Emscripten-documented
        idiom for this exact situation: `-DCMAKE_FIND_ROOT_PATH=/root/lib`
        added to `compile/libsndfile/Dockerfile`'s `emcmake cmake`
        invocation — passed as a cache variable, Emscripten's own
        toolchain file's `list(APPEND CMAKE_FIND_ROOT_PATH
        "${EMSCRIPTEN_SYSROOT}")` appends the sysroot onto it rather than
        replacing it, so both paths get searched. Verified fixed for real:
        `FLAC_LIBRARY`/`OGG_LIBRARY`/`OPUS_LIBRARY` all resolved to their
        real `/root/lib/lib/*.a` paths and `HAVE_EXTERNAL_XIPH_LIBS` became
        `1` in the rebuilt `config.h`.
      - `detectFormat()` (`compile/src/binding.cpp`) widened: the `fLaC`/
        `OggS` magic-byte cases, previously `KnownUnsupported`, now route
        to the same `SndFileAudioFileReader` path as RIFF/AIFF (the enum
        case itself renamed `RiffOrAiff` → `Sndfile` to match).
      - **Real, minor limitation found during testing, not a build bug**:
        the first Ogg Vorbis test file (`ffmpeg -i song.flac -c:a libvorbis
        out.ogg`, no explicit `-map`) still failed — root-caused to
        `ffmpeg` muxing the source FLAC's attached cover-art picture as a
        *second, non-audio Ogg logical bitstream* (confirmed via
        `ffprobe`: the audio track ends up as `Stream #0:1`, not `#0:0`),
        which libsndfile's Ogg demuxer doesn't recognise as a valid single
        stream and reports as unimplemented. An audio-only Ogg Vorbis file
        (`ffmpeg -map 0:a ...` — the standard shape essentially all real
        Ogg Vorbis files and encoders produce; embedding cover art as a
        second logical bitstream this way is not how Ogg tooling normally
        stores artwork) decodes correctly. Documented in `README.md`'s
        format table rather than chased further, same "empirically test,
        document real gaps honestly" approach as decision 16's numeric
        `TCON` genre-code gap.

    - **M4A/AAC**, via a genuinely new `M4aAudioFileReader.h`/`.cpp`
      (`compile/src/`) — audiowaveform has no AAC reader to extract, so
      this is this project's own code, modeled directly on the real
      `Mp3AudioFileReader.cpp` pipeline (fetched from GitHub while
      designing it) implementing the same `AudioFileReader` interface.
      Uses `libfaad2`'s (`knik0/faad2`, actively maintained fork)
      `NeAACDec*` decoder API plus its own `frontend/mp4read.c` MP4 box
      demuxer (both licenses re-confirmed from decision 9's correction #2:
      GPL-2.0-or-later core decoder, GPL-3.0-or-later demuxer — both
      compatible with this project's own GPL-3.0-or-later).
      `compile/libfaad2/Dockerfile` builds only the core `faad` library
      (`-DFAAD_BUILD_CLI=OFF`, skipping the CLI frontend's own extra
      dependencies entirely); `mp4read.c`/`mp4read.h`/`unicode_support.c`/
      `unicode_support.h` are pulled directly from that build stage's
      still-present source tree into `compile/audiowaveform/Dockerfile`
      (no second download), compiled as real C via the same explicit
      `emcc -c` step already used for `bstdfile.c`/`pdjson.c` (`mp4read.h`
      doesn't self-wrap in `extern "C"` the way `BStdFile.h`/`pdjson.h` do,
      so `M4aAudioFileReader.cpp` wraps its own `#include "mp4read.h"`
      instead). **Worked correctly on the very first real build and the
      very first real test run — no debugging needed**, unlike the
      libsndfile-based formats above.
    - **Verification, not just "it built"**: the full real FLAC album in
      `assets/` (all 6 tracks, not just a short synthetic clip) decodes
      correctly; MP3 ID3 tag/cover-art extraction re-tested and confirmed
      unaffected (regression check). `node-builds/audiowaveform.wasm` grew
      from ~640KB to ~980KB with all five new codec libraries linked in.
    - `matrix.json`/`compile/Makefile` gained five new entries
      (`libogg`/`libflac`/`libvorbis`/`libopus`/`libfaad2`), all
      `github-release`-tracked like the rest — noted in `matrix.json`
      itself that `xiph/ogg`/`xiph/vorbis`/`xiph/opus`'s real GitHub tags
      carry a `v` prefix (`v1.3.6` etc.) that `update-lib-versions.mjs`'s
      existing `normalizeVersion()` already strips before storing, so each
      Dockerfile's own `wget` URL adds the `v` back rather than storing it
      in the matrix (`xiph/flac`'s tags have no such prefix).

19. **WebM audio (Vorbis or Opus) added, built, and verified working
    (2026-09-12), requested by the user right after decision 18 landed.**
    WebM restricts its audio codecs to Vorbis and Opus — both already
    vendored (decision 18) — so this only needed a Matroska/WebM demuxer,
    not new codec work. Chose Mozilla's **`nestegg`**
    (`mozilla/nestegg`) over `libwebm`/FFmpeg for the same "one targeted
    lib at a time" reason `mp4read.c` was chosen for M4A over a general
    media framework: genuinely minimal (one `include/nestegg/nestegg.h` +
    one `src/nestegg.c`), and a clean callback-based (`nestegg_io`)
    demuxing API rather than a file-format-specific one. License verified
    directly (not assumed): ISC, permissive, GPL-3.0-or-later compatible.
    No real release tags exist upstream (`AC_INIT` pins a permanent
    "0.1git" dev version, confirmed via `gh api repos/mozilla/nestegg/tags`
    — zero results) — pinned to a commit SHA in `matrix.json` instead, same
    treatment as `libmad`/`libid3tag`'s dead-upstream pins. One real
    build-time difference from every other autotools lib here: the GitHub
    source archive at that SHA has no pre-generated `configure` (only ever
    produced by a real release, which never happened) — `compile/nestegg/
    Dockerfile` runs `autoreconf -fi` itself (automake/autoconf/libtool
    already in the base image).
    - **`compile/src/WebmAudioFileReader.h`/`.cpp`**: genuinely new code
      for this project (audiowaveform has no WebM reader), implementing
      `AudioFileReader` the same way `M4aAudioFileReader` does. `nestegg`
      only demultiplexes — it hands back raw per-track packets, not
      decoded audio — so this file also drives the actual decode: Vorbis
      via libvorbis's raw `vorbis_synthesis_*` API (feeding the three
      Vorbis header packets `nestegg_track_codec_data()` already splits
      out of the container's CodecPrivate, no manual Xiph-lacing parsing
      needed), Opus via the much simpler `opus_decode()` (fixed at Opus's
      own mandatory 48kHz internal rate per RFC 7845, not whatever rate
      the container metadata happens to report). Scope: Vorbis/Opus audio
      tracks only, matching what WebM (as opposed to the more permissive
      Matroska/`.mkv` container it's a restricted profile of) actually
      allows — `detectFormat()` in `compile/src/binding.cpp` sniffs the
      standard EBML header magic bytes (`0x1A 0x45 0xDF 0xA3`), the same
      signature `nestegg`'s own `nestegg_sniff_webm()`/`nestegg_sniff_mkv()`
      check first.
    - **Both the container-build and the final link succeeded on the very
      first real attempt, and so did the very first runtime test** —
      unlike decision 18's libsndfile-based formats, which needed real
      debugging (the `CMAKE_FIND_ROOT_PATH` fix) before they worked.
    - **Verified against real audio, not just a short synthetic clip**:
      both a 5-second Vorbis-in-WebM and Opus-in-WebM file, then a full
      6-minute-plus real track (from `assets/`) re-encoded to Opus-in-WebM
      — all decode to real peaks with correct `sample_rate`/`channels`.
      Full 9-format regression suite (MP3/WAV/AIFF/FLAC/Ogg-Vorbis/Ogg-
      Opus/M4A-AAC/WebM-Vorbis/WebM-Opus) re-run clean afterward, confirming
      no regression in the formats decision 18 had just fixed.
    - `matrix.json`/`compile/Makefile` gained a `nestegg` entry/target,
      standalone (only depends on `base-image`, since decoding reuses the
      already-vendored `libvorbis`/`libopus`); `audiowaveform-wasm`'s
      prerequisites and final `em++` link line gained it accordingly.

## Current status

**Builds, runs, and is published (2026-09-12).** `make audiowaveform-wasm`
produces a real, working `node-builds/audiowaveform.wasm` (~1MB) +
`audiowaveform.js` (a genuine ES module — `-s EXPORT_ES6=1`, decision 17),
exporting `extractAudioPeaks(bytes, samplesPerPixel)`,
`getId3Tags(mp3Bytes)`, and `getId3CoverArt(mp3Bytes)`. All three are
extensively runtime-tested (decisions 16, 18, and 19), not just built: the
full format matrix now passes for real (MP3/WAV-16/24/float/AIFF/FLAC/
Ogg-Vorbis/Ogg-Opus/M4A-AAC/WebM-Vorbis/WebM-Opus all produce real peaks —
decisions 18 and 19), 25 real tagged MP3s from the user's own library
(peaks + tags + cover art, 0 failures/crashes), and a deliberately
adversarial ID3 batch (ID3v2.3, ID3v2.4, ID3v1-only, unicode/emoji/Cyrillic,
long strings, numeric genre codes). This repo is pushed to
`https://github.com/php-kirigami/audiowaveform-wasm-compiler`; the
**published npm package it feeds, `@kirigami/audiowaveform-wasm@1.0.0`,
lives in a different repo** (`kirigami/packages/audiowaveform-wasm/` —
decision 17) and is live on the npm registry right now — this repo only
produces the raw compiled artifacts, same division of responsibility as
`php-wasm-compiler` vs. `@kirigami/php-wasm`.

**Not yet done / open questions:**

- ~~Run the actual build for the first time~~ — done, see decision 16 for
  the full debugging history (config.sub, CFLAGS, libz, boost headers,
  C/C++ mangling, missing stream globals, the stack-size crash).
- ~~Read `audiowaveform`'s actual source tree to confirm the boost
  dependency scope of the core~~ — done, see decision 8.
- ~~Decide MP3-only vs. multi-format (libsndfile) for v1~~ — resolved by
  decision 9 (MP3 primary) and **wired in** by decision 13 (`libsndfile`
  Dockerfile + Makefile target, WAV/AIFF/RAW only — FLAC/Ogg/Opus
  deferred, see decision 9's correction).
- ~~Decide whether the JS API needs file-based save/load~~ — resolved by
  decision 10 (buffer-only for v1).
- ~~Track dependency + `audiowaveform` versions like `php-wasm-compiler`~~
  — done, see decision 14 (`matrix.json`, `matrix-version.mjs`,
  `update-lib-versions.mjs`).
- ~~M4A/AAC support~~ — done, see decision 18 (`libfaad2` + `mp4read.c`,
  via the new `M4aAudioFileReader`).
- ~~Double-check `pdjson`'s license~~ — confirmed Unlicense (public domain,
  a real `UNLICENSE` file ships in `audiowaveform`'s `src/pdjson/`),
  GPL-3.0-compatible, no concern.
- ~~Design the actual Embind API surface~~ — `extractAudioPeaks()`,
  `getId3Tags()`, `getId3CoverArt()` all exist, built, and extensively
  tested (decision 16). Still open: whether `PixelsPerSecondScaleFactor`
  should be exposed as an alternative to `samplesPerPixel`, whether
  `WaveformRescaler` should be a separate bound function, and resolving
  numeric-only `TCON` genre codes (e.g. `"17"`) to their text name via
  `id3_genre_name()` — found as a real, minor gap during decision 16's
  adversarial ID3 testing, not yet fixed.
- ~~Repo/package naming~~ — this repo is `audiowaveform-wasm-compiler`
  (matching `php-wasm-compiler`'s naming pattern); the published package
  is `@kirigami/audiowaveform-wasm` (decision 17), live on npm as `1.0.0`.
- ~~GitHub remote~~ — **created and pushed 2026-09-12**:
  `https://github.com/php-kirigami/audiowaveform-wasm-compiler`. Claude
  Code's own auto-mode classifier had refused `gh repo create` for a new
  *public* repo directly ("Create Public Surface"); the user created the
  (empty, un-initialized-with-README) repo themselves, this session then
  `git remote add origin` + `git push -u origin main`'d the local history.
  One hiccup: GitHub had actually auto-initialized it with a one-line
  placeholder `README.md` despite being asked not to — resolved with
  `git fetch` + `git rebase origin/main` (keeping this repo's fuller
  README on conflict) rather than a force-push.
- No `compile/cli.mjs`-equivalent entry point yet — decision 12
  deliberately skipped the interactive-CLI/config-file apparatus for now
  (two fixed-version libs, no config matrix to drive); decision 14 only
  added the version-*tracking* half (`matrix-version.mjs`/
  `update-lib-versions.mjs` as standalone scripts, not a unified CLI). The
  Makefile is invoked directly (`make audiowaveform-wasm`) rather than
  through a Node CLI wrapper. Revisit once/if the project's scope (M4A,
  more formats) grows enough to justify one.
- ~~`libsndfile` not wired in~~ — done, see decision 13 (WAV/AIFF/RAW) and
  decision 18 (FLAC/Ogg-Vorbis/Ogg-Opus, `libFLAC`/`libogg`/`libvorbis`/
  `libopus` now vendored and wired in).

- ~~WebM audio~~ — done, see decision 19 (`nestegg` demuxer + the new
  `WebmAudioFileReader`, decoding via the already-vendored libvorbis/
  libopus).

**Still open:**

- Ogg Vorbis's "second, non-audio logical bitstream" limitation (decision
  18) — not planned to be fixed, just documented; revisit only if it turns
  out to matter for real files the Kirigami player actually needs to
  handle.
- WebM scope is Vorbis/Opus audio tracks only (decision 19) — matches what
  WebM itself restricts audio to, so this isn't considered a gap, but
  worth noting explicitly: a `.webm`-extensioned file with some other
  audio codec (not standard WebM) or a full Matroska `.mkv` file with a
  non-Vorbis/Opus audio track (e.g. AC3, FLAC-in-Matroska) will return
  `null`, same as any other unsupported format — not attempted, no
  evidence yet it's needed for the Kirigami player's real use case.
