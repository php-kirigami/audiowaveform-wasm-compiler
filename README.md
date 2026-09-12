# audiowaveform-wasm-compiler

Compiles [BBC's `audiowaveform`](https://github.com/bbc/audiowaveform) to
WebAssembly with a Node.js programming interface, for use in Kirigami
plugins (starting with an MP3 player plugin that needs waveform peak data
for its UI).

Status: scaffolding only, no build pipeline yet. See `CLAUDE.md` for the
architecture decisions made so far.

## License

GPL-3.0-or-later, inherited from `audiowaveform` itself (which links
`libmad`, GPL-2.0).
