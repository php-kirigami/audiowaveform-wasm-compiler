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
     - `FileHandle.cpp/.h` / `FileUtil.cpp/.h`: only needed if the JS API
       should also support save/load to/from the wasm virtual filesystem
       (`.dat`/`.json` on disk) rather than always working purely on
       in-memory buffers — **not yet decided**, leaning toward skipping
       these for a pure buffer-in/buffer-out API and revisiting if a
       file-based mode turns out to be useful.
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

## Current status

**Scaffolding only (2026-09-12).** Created while `php-wasm-compiler`'s
build pipeline runs in parallel, per the user's request to start this
project in the meantime. So far: repo initialized, `LICENSE` (GPL-3.0,
fetched via `gh api licenses/gpl-3.0`), this file, `README.md`. No source
extraction, no Dockerfile, no build pipeline yet.

**Not yet done / open questions:**

- ~~Read `audiowaveform`'s actual source tree to confirm the boost
  dependency scope of the core~~ — done, see decision 8.
- Decide MP3-only vs. multi-format (libsndfile) for v1.
- Decide whether the JS API needs file-based `.dat`/`.json` save/load
  (`FileHandle`/`FileUtil`) or purely in-memory buffers (decision 8's
  last bullet).
- Double-check `pdjson`'s license before vendoring (decision 8) — not yet
  confirmed, though it's a small, widely-reused JSON library.
- Design the actual Embind API surface (function names, options shape,
  return shape matching `waveform-data.js`'s expected format).
- Repo/package naming: this repo is `audiowaveform-wasm-compiler`
  (matching `php-wasm-compiler`'s naming pattern); the published npm
  package name is not yet decided (candidate: `@kirigami/audiowaveform-wasm`).
- GitHub remote: not yet created. `php-wasm-compiler`/`kirigami`/
  `kiribuild` all live under the `php-kirigami` GitHub org — presumably
  this repo would too, but the remote hasn't been created or confirmed
  with the user yet.
- No Docker base-image / Makefile / CLI scaffolding yet — the plan is to
  mirror `php-wasm-compiler`'s structure (`compile/base-image/`,
  `compile/<lib>/Dockerfile` per third-party lib, `compile/Makefile`,
  `compile/cli.mjs`) where it makes sense, adapted for the simpler
  single-module (no JSPI/dylink) build.
