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
- ✅ MP3 (`libmad` + `libid3tag`) and WAV/AIFF/RAW (`libsndfile`) today; FLAC/Ogg/Opus (needs `libFLAC`/`libogg`/`libvorbis`/`libopus`) and M4A/AAC (`libfdk-aac`) planned
- ❌ No CLI, no image rendering — that's `audiowaveform`'s own job, and JS's job on the consuming side

See [`CLAUDE.md`](CLAUDE.md) for the full architecture and decision history.

**Status: build pipeline authored, not yet run end-to-end.** Every Dockerfile, the `Makefile`, and the Embind wrapper exist, but no `docker build` has actually been executed against them yet — see `CLAUDE.md` for why (a concurrent build on a disk-constrained machine) and for the specific risk areas flagged for the first real build.

---

## Table of contents

- [audiowaveform-wasm-compiler](#audiowaveform-wasm-compiler)
  - [Overview](#overview)
  - [Table of contents](#table-of-contents)
  - [Requirements](#requirements)
  - [Building](#building)
  - [Usage](#usage)
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
const createModule = require('./node-builds/audiowaveform.js');
const fs = require('fs');

const module = await createModule();
const mp3Bytes = fs.readFileSync('song.mp3');

const peaks = module.extractMp3Peaks(mp3Bytes, 512);
// { version: 2, channels: 2, sample_rate: 44100, samples_per_pixel: 512,
//   bits: 16, length: 1234, data: [...] }

// WAV/AIFF/RAW instead of MP3:
const wavPeaks = module.extractWavPeaks(fs.readFileSync('song.wav'), 512);
```

---

## License

`GPL-3.0-or-later` — inherited from `audiowaveform` itself, which links `libmad` (GPL-2.0). See [LICENSE](./LICENSE) for the full text.

---

## Author

Maxime Larrivée-Roy, 2026
