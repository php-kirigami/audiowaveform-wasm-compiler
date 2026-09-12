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

## Current status

**Scaffolding + a full, unbuilt Docker/Emscripten pipeline, now with
version tracking (2026-09-12).** Created while `php-wasm-compiler`'s build
pipeline runs (on and off) in parallel, per the user's request to start
this project in the meantime — and, once that concurrent build revealed
this machine only has 8.6GB free on `C:` (decision 11), deliberately kept
to *authoring* (Dockerfiles, Makefile, the Embind wrapper, a real
generated-and-verified patch, `matrix.json` + its version-checking
scripts) rather than *running* anything build-heavy, to avoid competing
for disk/RAM with the PHP build. The two pure-Node scripts
(`matrix-version.mjs`, `update-lib-versions.mjs`) are the one exception —
network + local file I/O only, no Docker/disk cost, and both have actually
been run and verified working (decision 14). Repo initialized and pushed
to `https://github.com/php-kirigami/audiowaveform-wasm-compiler`; `LICENSE`
(GPL-3.0), this file, `README.md` (rewritten to follow the Kirigami
ecosystem's README convention — logo, badges, Table of contents — per the
user's request), the full `compile/` pipeline (decisions 12-13), and
`matrix.json` (decision 14) are all in place.

**Not yet done / open questions:**

- Run the actual build for the first time once `php-wasm-compiler`'s
  build finishes and disk headroom is confirmed again (decision 11) —
  resolves every "not yet verified" flag left in decisions 12-13's
  Dockerfiles (libmad's FPM selection, libid3tag/zlib, the full link step).
- ~~Read `audiowaveform`'s actual source tree to confirm the boost
  dependency scope of the core~~ — done, see decision 8.
- ~~Decide MP3-only vs. multi-format (libsndfile) for v1~~ — resolved by
  decision 9 (MP3 primary) and **wired in** by decision 13 (`libsndfile`
  Dockerfile + Makefile target + `extractWavPeaks()` in `binding.cpp`,
  WAV/AIFF/RAW only — FLAC/Ogg/Opus deferred, see decision 9's correction).
- ~~Decide whether the JS API needs file-based save/load~~ — resolved by
  decision 10 (buffer-only for v1).
- ~~Track dependency + `audiowaveform` versions like `php-wasm-compiler`~~
  — done, see decision 14 (`matrix.json`, `matrix-version.mjs`,
  `update-lib-versions.mjs`).
- M4A/AAC support (decision 9) — still deferred to v2. Revised plan
  (decision 9's correction #2): `libfaad2` + its own bundled `mp4read.c`
  demuxer (both GPL-2/3-or-later, unlike the originally-considered
  `libfdk-aac` which is very likely GPL-incompatible). No
  `compile/libfaad2/Dockerfile` written yet.
- ~~Double-check `pdjson`'s license~~ — confirmed Unlicense (public domain,
  a real `UNLICENSE` file ships in `audiowaveform`'s `src/pdjson/`),
  GPL-3.0-compatible, no concern.
- ~~Design the actual Embind API surface~~ — `extractMp3Peaks()` and
  `extractWavPeaks()` exist in decision 12/13's `binding.cpp`, but both are
  untested and likely to need real API-shape iteration once actually
  compiled and exercised from Node (parameter names, whether
  `PixelsPerSecondScaleFactor` should be exposed as an alternative to
  `samplesPerPixel`, whether `WaveformRescaler` should be a separate bound
  function, etc.).
- Repo/package naming: this repo is `audiowaveform-wasm-compiler`
  (matching `php-wasm-compiler`'s naming pattern); the published npm
  package name is not yet decided (candidate: `@kirigami/audiowaveform-wasm`).
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
- ~~`libsndfile` not wired in~~ — done, see decision 13 (WAV/AIFF/RAW
  only; FLAC/Ogg/Opus still need `libFLAC`/`libogg`/`libvorbis`/`libopus`
  vendored, per decision 9's correction).
