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

## Current status

**Scaffolding only (2026-09-12).** Created while `php-wasm-compiler`'s
build pipeline runs in parallel, per the user's request to start this
project in the meantime. So far: repo initialized, `LICENSE` (GPL-3.0,
fetched via `gh api licenses/gpl-3.0`), this file, `README.md`. No source
extraction, no Dockerfile, no build pipeline yet.

**Not yet done / open questions:**

- Read `audiowaveform`'s actual source tree to confirm the boost
  dependency scope of the core (decision 4) before assuming it's
  CLI-only.
- Decide MP3-only vs. multi-format (libsndfile) for v1.
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
