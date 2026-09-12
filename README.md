<div align="center">

<img src="https://zmotrin.github.io/assets/kirigami/kirigami-logo-universal.svg" alt="Kirigami" width="400" />

---

# audiowaveform-wasm-compiler

**Compiles BBC's `audiowaveform` to WebAssembly (Docker + Emscripten) — Node.js only.**  
Builds a waveform-peak-extraction module for the **[Kirigami](https://github.com/php-kirigami)** static site generator.

[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-yellow)](./LICENSE)
[![Node.js >=24.0.0](https://img.shields.io/badge/node-%3E%3D24.0.0-brightgreen)](https://nodejs.org)
[![Website](https://img.shields.io/badge/website-php--kirigami.github.io-1f6b4a)](https://php-kirigami.github.io)

</div>

---

## Overview

`audiowaveform-wasm-compiler` extracts the peak-generation core of [BBC's `audiowaveform`](https://github.com/bbc/audiowaveform) — not its CLI, not its PNG renderer — and compiles it to a single WebAssembly module with a plain Node.js API, for a Kirigami **MP3 player plugin** that needs waveform data for its UI (SoundCloud-style, under the seek bar):

- ✅ **Node.js** only, no browser target
- ✅ One monolithic wasm module — no JSPI, no Asyncify, no dylink side-modules (peak extraction is synchronous, CPU-bound work)
- ✅ Buffer in, JS object out — output shaped like `audiowaveform`'s own documented peaks format, so [`waveform-data.js`](https://github.com/bbc/waveform-data.js) can consume it directly
- ✅ MP3 (`libmad` + `libid3tag`) and WAV/AIFF/RAW (`libsndfile`) today; FLAC/Ogg/Opus (needs `libFLAC`/`libogg`/`libvorbis`/`libopus`) and M4A/AAC (`libfaad2` — not `libfdk-aac`, which is very likely GPL-incompatible) planned
- ❌ No CLI, no image rendering — that's `audiowaveform`'s own job, and JS's job on the consuming side

See [`CLAUDE.md`](CLAUDE.md) for the full architecture and decision history.

**Status: builds and runs.** The full pipeline (`make audiowaveform-wasm`) produces a working `audiowaveform.wasm` + `audiowaveform.js`, smoke-tested against real audio files — see [Format support](#format-support) below and `CLAUDE.md` for the full build/debugging history.

---

## Table of contents

- [audiowaveform-wasm-compiler](#audiowaveform-wasm-compiler)
  - [Overview](#overview)
  - [Table of contents](#table-of-contents)
  - [Requirements](#requirements)
  - [Building](#building)
  - [Usage](#usage)
  - [Format support](#format-support)
  - [License](#license)
  - [Author](#author)

---

## Requirements

- Docker
- GNU Make
- Node.js `>= 24.0.0` — to consume the compiled module (the build pipeline itself is pure Docker/Make, no Node tooling yet)

---

## Building

```bash
cd compile
make base-image
make audiowaveform-wasm
```

Output lands in `node-builds/`. Library versions (`libmad`, `libid3tag`, `libsndfile`, `audiowaveform` itself) are tracked in [`matrix.json`](matrix.json) — refresh with `node compile/update-lib-versions.mjs --write` — and resolved into the build automatically via `compile/Makefile`. No `config.yaml`/interactive CLI wrapper yet (see `CLAUDE.md` for why that's deliberate at this project's current size).

---

## Usage

```js
import createModule from './node-builds/audiowaveform.js';
import fs from 'node:fs';

const module = await createModule();
const mp3Bytes = fs.readFileSync('song.mp3');

const peaks = module.extractAudioPeaks(mp3Bytes, 512);
// { version: 2, channels: 2, sample_rate: 44100, samples_per_pixel: 512,
//   bits: 16, length: 1234, data: [...] }

// Same function for WAV/AIFF/RAW — format is auto-detected, tries MP3
// first then falls back to libsndfile.
const wavPeaks = module.extractAudioPeaks(fs.readFileSync('song.wav'), 512);

// ID3 tag metadata and embedded cover art (MP3 only):
const tags = module.getId3Tags(mp3Bytes);
// { title, artist, album, albumArtist, year, track, genre } — each a
// string or null if that frame isn't present; null if there's no tag at all

const cover = module.getId3CoverArt(mp3Bytes);
// { mimeType, pictureType, data } — data is a plain array of byte values
// (0-255); null if there's no embedded cover art
```

---

## Format support

Tested 2026-09-12 against real audio — a FLAC album re-encoded to each format with `ffmpeg`, plus 25 real, randomly-sampled tagged MP3s (not just assumed from library capabilities):

| Format | Status | Notes |
| --- | --- | --- |
| MP3 | ✅ Works | `libmad` + `libid3tag`; peaks, tags, and cover art all verified against 25 real files (0 failures, 0 crashes) |
| WAV (16-bit PCM) | ✅ Works | |
| WAV (24-bit PCM) | ✅ Works | |
| WAV (32-bit float) | ✅ Works | |
| AIFF | ✅ Works | |
| FLAC | ❌ Not yet | `libsndfile` built with `ENABLE_EXTERNAL_LIBS=OFF` — needs `libFLAC` vendored |
| Ogg Vorbis | ❌ Not yet | same — needs `libogg` + `libvorbis` |
| Opus | ❌ Not yet | same — needs `libogg` + `libopus` |
| M4A/AAC | ❌ Not yet | needs a new decoder path entirely, planned via `libfaad2` (not `libfdk-aac` — GPL-incompatible license) — see `CLAUDE.md` |

`extractAudioPeaks()` returns `null` for unsupported formats — never crashes.

Unsupported formats fail cleanly (the function returns `null`), never crash.

---

## License

`GPL-3.0-or-later` — inherited from `audiowaveform` itself, which links `libmad` (GPL-2.0). See [LICENSE](./LICENSE) for the full text.

---

## Author

Maxime Larrivée-Roy, 2026
