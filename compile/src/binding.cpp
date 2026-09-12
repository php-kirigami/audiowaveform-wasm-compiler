// Embind wrapper exposing a Node.js-friendly, buffer-in/buffer-out API
// around audiowaveform's real peak-generation pipeline (CLAUDE.md
// decisions 4, 5, 8, 10, 13). This file is ours; everything it calls into
// (Mp3AudioFileReader, SndFileAudioFileReader, WaveformGenerator,
// WaveformBuffer, ...) is audiowaveform's own unmodified source, fetched
// at build time by ../audiowaveform/Dockerfile.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "AudioFileReader.h"
#include "Mp3AudioFileReader.h"
#include "SndFileAudioFileReader.h"
#include "WaveformGenerator.h"
#include "WaveformBuffer.h"
#include "Streams.h"

// libid3tag's own public header, not part of audiowaveform's source tree —
// installed by compile/libid3tag/Dockerfile into /root/lib/include,
// already on the include path this file compiles with (-I /root/lib/include
// in ../audiowaveform/Dockerfile). audiowaveform itself only uses libid3tag
// internally to *skip past* ID3 tags before decoding (Mp3AudioFileReader's
// private skipId3Tags()) — it never reads tag *values*. Exposing real tag
// data (title/artist/album/...) is this project's own addition, using
// libid3tag's public API directly, requested by the user 2026-09-12 since
// the library is already vendored here.
#include <id3tag.h>

using namespace emscripten;

namespace {

// A trivial discard-everything streambuf/ostream pair, used below instead
// of std::cout/std::cerr.
//
// Originally added chasing what looked like a std::cout-specific bug
// (routing output_stream at std::cout crashed with `RuntimeError: table
// index is out of bounds` inside Emscripten's libc++ stdout streambuf).
// The REAL root cause, found right after: this build's default wasm stack
// (Emscripten's default STACK_SIZE, 64KB) is too small for the call depth
// libc++'s chained `operator<<` / `ostream::sentry` machinery needs —
// confirmed by adding `-s STACK_SIZE=5MB` in ../audiowaveform/Dockerfile,
// which made writes to a plain std::cout-backed stream work fine too. So
// this null-stream redirect isn't load-bearing for correctness anymore —
// kept anyway as a deliberate design choice: a library function silently
// writing CLI-style "Generating waveform data..." text to the embedding
// Node process's real stdout/stderr would be surprising, unwanted
// behavior for a programmatic API, regardless of whether it also crashes.
class NullStreamBuf : public std::streambuf {
    protected:
        int overflow(int c) override { return c; }
};

NullStreamBuf nullStreamBuf;
std::ostream nullStream(&nullStreamBuf);

} // namespace

// Streams.h only declares these `extern`; audiowaveform's own Main.cpp
// (excluded from this build, decision 4) is normally what defines them.
// Log.cpp needs a real definition to link since it's part of the extracted
// core (decision 13 — required by SndFileAudioFileReader's ProgressReporter
// dependency chain).
std::ostream& output_stream = nullStream;
std::ostream& error_stream = nullStream;

namespace {

// Every AudioFileReader subclass in audiowaveform reads from a real file
// path, not an in-memory buffer — there is no in-memory-buffer entry point
// to hook into without patching audiowaveform's own code, which we're
// deliberately not doing (CLAUDE.md decision 4: extract, don't fork).
// Node/Emscripten's default MEMFS makes this cheap: stage the incoming
// bytes as a temp file there (no real disk I/O), then let the real
// reader's open() work completely unmodified. libsndfile auto-detects
// format from the file's actual header bytes, not the filename/extension,
// so one fixed path serves both readers.
const char* const TEMP_INPUT_PATH = "/tmp/kirigami-audiowaveform-input";

// Builds a JS object matching audiowaveform's own documented JSON peaks
// format (version/channels/sample_rate/samples_per_pixel/bits/length/data)
// — see CLAUDE.md decision 6. Returning a real object (not a serialized
// JSON string) so callers get typed data for free; JSON.stringify() on the
// result reconstructs the exact file format audiowaveform's own `-o
// foo.json` CLI flag would have produced.
val bufferToPeaksObject(const WaveformBuffer& buffer) {
    const int channels = buffer.getChannels();
    const int size = buffer.getSize();

    val data = val::array();
    int index = 0;

    for (int i = 0; i < size; ++i) {
        for (int channel = 0; channel < channels; ++channel) {
            data.set(index++, buffer.getMinSample(channel, i));
            data.set(index++, buffer.getMaxSample(channel, i));
        }
    }

    val result = val::object();
    result.set("version", 2);
    result.set("channels", channels);
    result.set("sample_rate", buffer.getSampleRate());
    result.set("samples_per_pixel", buffer.getSamplesPerPixel());
    result.set("bits", buffer.getBits());
    result.set("length", size);
    result.set("data", data);

    return result;
}

// Shared pipeline for every reader: stage bytes into MEMFS, open() the
// given reader against that path, run it through WaveformGenerator, tear
// down the temp file. Reimplements the shape of
// OptionHandler::generateWaveformData()'s real pipeline
// (createAudioFileReader -> WaveformGenerator -> WaveformBuffer) without
// audiowaveform's own CLI/Options/OptionHandler machinery, which this
// project doesn't vendor at all (CLAUDE.md decision 4).
val extractPeaks(AudioFileReader& reader, const std::string& bytes, int samplesPerPixel) {
    {
        FILE* file = fopen(TEMP_INPUT_PATH, "wb");

        if (file == nullptr) {
            return val::null();
        }

        fwrite(bytes.data(), 1, bytes.size(), file);
        fclose(file);
    }

    const bool opened = reader.open(TEMP_INPUT_PATH, false);

    if (!opened) {
        remove(TEMP_INPUT_PATH);
        return val::null();
    }

    WaveformBuffer buffer;
    SamplesPerPixelScaleFactor scaleFactor(samplesPerPixel);

    // split_channels = false: emit one merged waveform, not one per
    // channel — the common case for a player UI. Worth exposing as a
    // real parameter later if a stereo-split view turns out to be wanted.
    WaveformGenerator processor(buffer, false, scaleFactor);

    const bool success = reader.run(processor);

    remove(TEMP_INPUT_PATH);

    // buffer.getSize() == 0 is treated as failure too, not just a valid
    // (if useless) empty result: found necessary the hard way when
    // extractAudioPeaks() started trying Mp3AudioFileReader against
    // clearly-non-MP3 input (FLAC/Ogg/Opus/M4A) — open()/run() both
    // returned true, but produced a buffer with sample_rate 0 and zero
    // peaks, because libmad's frame scan can find zero valid MP3 sync
    // frames in arbitrary bytes without that itself being treated as a
    // decode error. Without this check, extractAudioPeaks() would report
    // a bogus "success" for literally any unsupported format instead of
    // falling through to try SndFileAudioFileReader.
    if (!success || buffer.getSize() == 0) {
        return val::null();
    }

    return bufferToPeaksObject(buffer);
}

} // namespace

// Picks the right reader by sniffing real container magic bytes, rather
// than trying one reader and treating failure as "wrong format" — that
// approach was tried first and was genuinely unreliable: found via the
// format matrix test that Mp3AudioFileReader::open()/run() can both
// return true against FLAC/Ogg input, because libmad's frame-sync scanner
// occasionally finds a small number of coincidentally-valid-looking sync
// words in compressed, high-entropy binary data (FLAC/Ogg bitstreams
// included) — producing a small but nonzero, garbage "successful" result
// instead of cleanly failing. A `buffer.getSize() == 0` check (tried
// first) wasn't sufficient either — some inputs decoded a handful of real
// garbage points, not exactly zero. Real magic bytes are cheap, exact,
// and don't have this false-positive problem.
enum class DetectedFormat {
    RiffOrAiff, // WAV ("RIFF...WAVE") or AIFF ("FORM...AIFF"/"AIFC") — libsndfile
    KnownUnsupported, // FLAC ("fLaC"), Ogg (Vorbis/Opus, "OggS"), MP4/M4A ("....ftyp") — decision 9/13
    Unknown // ID3-tagged or bare-sync MP3, or anything else — try Mp3AudioFileReader
};

DetectedFormat detectFormat(const std::string& bytes) {
    if (bytes.size() >= 4 &&
        (bytes.compare(0, 4, "RIFF") == 0 || bytes.compare(0, 4, "FORM") == 0)) {
        return DetectedFormat::RiffOrAiff;
    }

    if (bytes.size() >= 4 &&
        (bytes.compare(0, 4, "fLaC") == 0 || bytes.compare(0, 4, "OggS") == 0)) {
        return DetectedFormat::KnownUnsupported;
    }

    // MP4-family containers (M4A/AAC included) store their box type at
    // offset 4, not 0 — the first 4 bytes are the box size, which varies.
    if (bytes.size() >= 8 && bytes.compare(4, 4, "ftyp") == 0) {
        return DetectedFormat::KnownUnsupported;
    }

    return DetectedFormat::Unknown;
}

} // namespace

// Extracts waveform peak data from an in-memory audio buffer of any
// supported format — callers don't need to know the format up front.
// Renamed from the original two-function split
// (extractMp3Peaks/extractWavPeaks) per the user's request, 2026-09-12.
// See detectFormat()'s comment above for why this dispatches on real
// magic bytes rather than a try-MP3-then-fall-back-on-failure approach.
//
// `samplesPerPixel` controls waveform resolution, same meaning as
// audiowaveform's own `--pixels-per-second` family of CLI options (see
// WaveformGenerator.h's ScaleFactor subclasses — SamplesPerPixelScaleFactor
// is the simplest one to wire up first; PixelsPerSecondScaleFactor is a
// natural follow-up parameter). Returns the peaks object (decision 6), or
// `null` if the format isn't supported or the file failed to decode.
val extractAudioPeaks(const std::string& bytes, int samplesPerPixel) {
    switch (detectFormat(bytes)) {
        case DetectedFormat::RiffOrAiff: {
            SndFileAudioFileReader reader;
            return extractPeaks(reader, bytes, samplesPerPixel);
        }

        case DetectedFormat::KnownUnsupported:
            return val::null();

        case DetectedFormat::Unknown:
        default: {
            Mp3AudioFileReader reader;
            return extractPeaks(reader, bytes, samplesPerPixel);
        }
    }
}

namespace {

// Separate temp path from TEMP_INPUT_PATH: harmless to share in practice
// (JS calls are synchronous, never overlapping), but keeping them distinct
// avoids any doubt if that ever changes.
const char* const TEMP_ID3_PATH = "/tmp/kirigami-audiowaveform-id3-input.mp3";

// Reads a single ID3v2 text-information frame (TIT2/TPE1/TALB/...) as a
// UTF-8 JS string, or null if the frame isn't present. Every such frame
// has the same shape: field 0 is the text encoding, field 1 is a
// STRINGLIST holding the actual value(s) — this is libid3tag's own
// documented convention (id3tag.h), not guessed; the same pattern real
// consumers of this library (e.g. mpg321, various id3-tag editors) use.
val getId3TextFrame(const id3_tag* tag, const char* frameId) {
    const id3_frame* frame = id3_tag_findframe(tag, frameId, 0);

    if (frame == nullptr || frame->nfields < 2) {
        return val::null();
    }

    const id3_field* field = id3_frame_field(frame, 1);

    if (field == nullptr) {
        return val::null();
    }

    const id3_ucs4_t* ucs4 = id3_field_getstrings(field, 0);

    if (ucs4 == nullptr) {
        return val::null();
    }

    id3_utf8_t* utf8 = id3_ucs4_utf8duplicate(ucs4);

    if (utf8 == nullptr) {
        return val::null();
    }

    val result(std::string(reinterpret_cast<const char*>(utf8)));
    free(utf8);

    return result;
}

// Shared by getId3Tags() and getId3CoverArt(): stage bytes into MEMFS,
// open via libid3tag. id3_file_open() handles both ID3v1 (trailer) and
// ID3v2 (header) tags automatically; when both are present, id3_file_tag()
// returns the merged/preferred one per libid3tag's own logic, not
// something this wrapper decides. Caller must id3_file_close() the result
// (if non-null) and remove(TEMP_ID3_PATH) when done.
id3_file* openId3File(const std::string& mp3Bytes) {
    FILE* file = fopen(TEMP_ID3_PATH, "wb");

    if (file == nullptr) {
        return nullptr;
    }

    fwrite(mp3Bytes.data(), 1, mp3Bytes.size(), file);
    fclose(file);

    return id3_file_open(TEMP_ID3_PATH, ID3_FILE_MODE_READONLY);
}

} // namespace

// Reads ID3 tag metadata (title/artist/album/...) from an in-memory MP3
// buffer, via libid3tag's own public API (not audiowaveform's code — see
// the #include <id3tag.h> comment above).
//
// Returns null if no tag is present at all; otherwise an object with each
// field either a string or null (frame not present in this particular
// file — e.g. many files have no album artist or comment).
//
// TDRC (ID3v2.4) vs TYER (ID3v2.3) both mean "year" — try the newer tag
// first, fall back to the older one, since real-world files use either
// depending on what encoded them.
val getId3Tags(const std::string& mp3Bytes) {
    id3_file* file = openId3File(mp3Bytes);

    if (file == nullptr) {
        remove(TEMP_ID3_PATH);
        return val::null();
    }

    const id3_tag* tag = id3_file_tag(file);

    val result = val::null();

    if (tag != nullptr) {
        result = val::object();
        result.set("title", getId3TextFrame(tag, "TIT2"));
        result.set("artist", getId3TextFrame(tag, "TPE1"));
        result.set("album", getId3TextFrame(tag, "TALB"));
        result.set("albumArtist", getId3TextFrame(tag, "TPE2"));

        val year = getId3TextFrame(tag, "TDRC");
        result.set("year", year.isNull() ? getId3TextFrame(tag, "TYER") : year);

        result.set("track", getId3TextFrame(tag, "TRCK"));
        result.set("genre", getId3TextFrame(tag, "TCON"));
    }

    id3_file_close(file);
    remove(TEMP_ID3_PATH);

    return result;
}

// Reads the embedded cover art (ID3v2 APIC frame) from an in-memory MP3
// buffer, if present. Field layout confirmed directly from libid3tag's own
// frametype.c (FIELDS(APIC) = TEXTENCODING, LATIN1, INT8, STRING,
// BINARYDATA — not guessed): field 1 is the MIME type, field 2 the
// ID3v2-defined picture type (3 = front cover, 4 = back cover, 0 = other,
// etc. — see the ID3v2 spec's APIC frame description for the full list),
// field 4 the raw image bytes.
//
// Returns null if there's no APIC frame; otherwise
// { mimeType, pictureType, data } where `data` is a plain array of byte
// values (0-255) — deliberately not a typed_memory_view, since the
// underlying bytes belong to the id3_frame and don't outlive
// id3_file_close() below; copying into a plain JS-owned array up front
// avoids any doubt about buffer lifetime, at the cost of being less
// memory-efficient for large cover art than a real Uint8Array would be.
val getId3CoverArt(const std::string& mp3Bytes) {
    id3_file* file = openId3File(mp3Bytes);

    if (file == nullptr) {
        remove(TEMP_ID3_PATH);
        return val::null();
    }

    const id3_tag* tag = id3_file_tag(file);
    val result = val::null();

    if (tag != nullptr) {
        const id3_frame* frame = id3_tag_findframe(tag, "APIC", 0);

        if (frame != nullptr && frame->nfields >= 5) {
            const id3_field* mimeField = id3_frame_field(frame, 1);
            const id3_field* typeField = id3_frame_field(frame, 2);
            const id3_field* dataField = id3_frame_field(frame, 4);

            id3_length_t length = 0;
            const id3_byte_t* bytes = dataField != nullptr
                ? id3_field_getbinarydata(dataField, &length)
                : nullptr;

            if (bytes != nullptr) {
                val data = val::array();

                for (id3_length_t i = 0; i < length; ++i) {
                    data.set(i, bytes[i]);
                }

                const id3_latin1_t* mime = mimeField != nullptr
                    ? id3_field_getlatin1(mimeField)
                    : nullptr;

                result = val::object();
                result.set(
                    "mimeType",
                    mime != nullptr
                        ? val(std::string(reinterpret_cast<const char*>(mime)))
                        : val::null()
                );
                result.set(
                    "pictureType",
                    typeField != nullptr ? static_cast<int>(id3_field_getint(typeField)) : 0
                );
                result.set("data", data);
            }
        }
    }

    id3_file_close(file);
    remove(TEMP_ID3_PATH);

    return result;
}

EMSCRIPTEN_BINDINGS(kirigami_audiowaveform) {
    function("extractAudioPeaks", &extractAudioPeaks);
    function("getId3Tags", &getId3Tags);
    function("getId3CoverArt", &getId3CoverArt);
}
