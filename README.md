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
- ✅ **MP3, fully verified** (`libmad` + `libid3tag`) — both CBR and VBR, every common bitrate/quality level, mono and stereo, MPEG1 and MPEG2 sample rates, with and without a Xing/LAME VBR header; see [Format support](#format-support) for the full 16-case matrix
- ✅ WAV/AIFF/RAW/FLAC/Ogg-Vorbis/Ogg-Opus (`libsndfile` + `libFLAC`/`libogg`/`libvorbis`/`libopus`), M4A/AAC (`libfaad2` — not `libfdk-aac`, which is very likely GPL-incompatible), and WebM (Vorbis/Opus audio, via Mozilla's `nestegg` demuxer)
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
// { version: 2, channels: 1, sample_rate: 44100, samples_per_pixel: 512,
//   bits: 16, length: 1234, data: [...] }
// Note: `channels` is always 1 — stereo (or any multichannel) input is
// downmixed into one merged waveform, matching audiowaveform's own CLI
// default (`split_channels=false`). It does not reflect the source
// file's real channel count.

// Same function for WAV/AIFF/FLAC/Ogg-Vorbis/Ogg-Opus/M4A-AAC/WebM — format
// is auto-detected by sniffing real container magic bytes (RIFF/FORM/fLaC/
// OggS -> libsndfile; ....ftyp -> M4aAudioFileReader/libfaad2; EBML header
// -> WebmAudioFileReader/nestegg; anything else -> MP3).
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
| MP3 | ✅ Works | `libmad` + `libid3tag`; peaks, tags, and cover art all verified against 25 real files (0 failures, 0 crashes) **plus a deliberate 16-case CBR/VBR encoding matrix** (see below) — both encoding modes, every common bitrate/quality, mono/stereo, MPEG1/MPEG2 sample rates |
| WAV (16-bit PCM) | ✅ Works | |
| WAV (24-bit PCM) | ✅ Works | |
| WAV (32-bit float) | ✅ Works | |
| AIFF | ✅ Works | |
| FLAC | ✅ Works | `libsndfile` + `libFLAC`, verified against the full real album in `assets/`, not just a short clip |
| Ogg Vorbis | ✅ Works | `libsndfile` + `libogg`/`libvorbis`. Known minor limitation: an Ogg file with a second, non-audio logical bitstream muxed in (e.g. an attached-picture stream some encoders produce) isn't recognised — plain audio-only Ogg Vorbis (the standard shape) works |
| Opus | ✅ Works | `libsndfile` + `libogg`/`libopus` |
| M4A/AAC | ✅ Works | `libfaad2` (not `libfdk-aac` — GPL-incompatible license) + its own `mp4read.c` MP4 demuxer, via this project's own `M4aAudioFileReader` (audiowaveform has no AAC reader of its own) — see `CLAUDE.md` |
| WebM (Vorbis or Opus audio) | ✅ Works | Mozilla's `nestegg` demuxes the container, decoding reuses `libvorbis`/`libopus` (already vendored for Ogg support) via this project's own `WebmAudioFileReader` — verified against a full 6+ minute real track, not just a short clip |

`extractAudioPeaks()` returns `null` for unsupported formats — never crashes.

### MP3 encoding modes tested

Beyond the 25 real-world files above, tested 2026-09-12 against a deliberate
16-file matrix generated with `ffmpeg`/`libmp3lame` from a real FLAC track
(60s clips, plus one full-length file), to confirm CBR *and* VBR both work
across the range real-world encoders produce — not just whatever encoding
the sample files happened to use:

| Case | Status |
| --- | --- |
| CBR 64 / 128 / 192 / 256 / 320 kbps | ✅ Works |
| VBR quality 0 (best, ~245kbps) / 4 (~165kbps) / 9 (worst, ~65kbps) | ✅ Works |
| Mono, CBR and VBR | ✅ Works |
| 48000 Hz (MPEG1 framing) | ✅ Works |
| 22050 Hz / 16000 Hz (MPEG2 framing — half-size frames vs. MPEG1) | ✅ Works |
| Full-length file (~6.5 min), VBR | ✅ Works |
| No Xing/LAME VBR header frame (`-write_xing 0`) | ✅ Works |
| Simple (non-joint) stereo vs. LAME's joint-stereo default | ✅ Works |

Every case produced real, non-zero peak data with a peak count matching
the file's actual duration. 16/16 passed, 0 failures.

### ID3 tag support

`getId3Tags()`/`getId3CoverArt()` tested against 25 real, randomly-sampled tagged MP3s (0 failures/crashes) plus a deliberately adversarial batch generated with `ffmpeg`:

| Case | Status | Notes |
| --- | --- | --- |
| ID3v2.3 | ✅ Works | |
| ID3v2.4 | ✅ Works | `year` tries `TDRC` (v2.4) then falls back to `TYER` (v2.3) |
| ID3v1-only (no v2 tag) | ✅ Works | numeric genre byte correctly resolved to text |
| Unicode (Japanese, emoji, Cyrillic, French accents mixed in one string) | ✅ Works | |
| Very long strings | ✅ Works | no truncation |
| Numeric-style `TCON` genre (e.g. `"17"`) | ⚠️ Partial | returned as the raw string, not resolved to a genre name via `id3_genre_name()` — a known, minor gap |

---

## License

`GPL-3.0-or-later` — inherited from `audiowaveform` itself, which links `libmad` (GPL-2.0). See [LICENSE](./LICENSE) for the full text.

---

## Author

Maxime Larrivée-Roy, 2026
