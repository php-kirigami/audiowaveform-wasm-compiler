// Embind wrapper exposing a Node.js-friendly, buffer-in/buffer-out API
// around audiowaveform's real peak-generation pipeline (CLAUDE.md
// decisions 4, 5, 8, 10, 13). This file is ours; everything it calls into
// (Mp3AudioFileReader, SndFileAudioFileReader, WaveformGenerator,
// WaveformBuffer, ...) is audiowaveform's own unmodified source, fetched
// at build time by ../audiowaveform/Dockerfile.
//
// NOT YET COMPILED OR TESTED (see CLAUDE.md decision 11 — no docker build
// has been run yet). Written from a direct reading of audiowaveform's
// AudioFileReader.h / Mp3AudioFileReader.h / SndFileAudioFileReader.h /
// WaveformGenerator.h / WaveformBuffer.h headers, not guessed.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdio>
#include <string>

#include "AudioFileReader.h"
#include "Mp3AudioFileReader.h"
#include "SndFileAudioFileReader.h"
#include "WaveformGenerator.h"
#include "WaveformBuffer.h"

using namespace emscripten;

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

    if (!success) {
        return val::null();
    }

    return bufferToPeaksObject(buffer);
}

} // namespace

// Extracts waveform peak data from an in-memory MP3 buffer.
// `samplesPerPixel` controls waveform resolution, same meaning as
// audiowaveform's own `--pixels-per-second` family of CLI options (see
// WaveformGenerator.h's ScaleFactor subclasses — SamplesPerPixelScaleFactor
// is the simplest one to wire up first; PixelsPerSecondScaleFactor is a
// natural follow-up parameter). Returns the peaks object (decision 6), or
// `null` on decode failure.
val extractMp3Peaks(const std::string& mp3Bytes, int samplesPerPixel) {
    Mp3AudioFileReader reader;
    return extractPeaks(reader, mp3Bytes, samplesPerPixel);
}

// Same as extractMp3Peaks, but for formats libsndfile understands: WAV,
// AIFF, RAW (CLAUDE.md decision 13 — this build's libsndfile is compiled
// with ENABLE_EXTERNAL_LIBS=OFF, so FLAC/Ogg-Vorbis/Opus are NOT supported
// yet despite being formats libsndfile can otherwise read).
val extractWavPeaks(const std::string& audioBytes, int samplesPerPixel) {
    SndFileAudioFileReader reader;
    return extractPeaks(reader, audioBytes, samplesPerPixel);
}

EMSCRIPTEN_BINDINGS(kirigami_audiowaveform) {
    function("extractMp3Peaks", &extractMp3Peaks);
    function("extractWavPeaks", &extractWavPeaks);
}
