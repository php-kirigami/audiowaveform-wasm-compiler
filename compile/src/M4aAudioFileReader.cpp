// See M4aAudioFileReader.h for context — this file is this project's own
// code, not audiowaveform's.

#include "M4aAudioFileReader.h"
#include "AudioProcessor.h"

// neaacdec.h already self-wraps in `extern "C" { ... }` (checked directly in
// its real source before relying on it), so no extra wrapping needed here.
#include <neaacdec.h>

// mp4read.h, unlike neaacdec.h, does NOT self-wrap in extern "C" (checked
// directly in its real source) — wrapped here instead, at the include site,
// same C/C++ boundary approach this project already uses for
// BStdFile.h/pdjson.h (see ../audiowaveform/Dockerfile's comment on why
// mp4read.c/unicode_support.c are compiled as plain C, not C++).
extern "C" {
#include "mp4read.h"
}

//------------------------------------------------------------------------------

M4aAudioFileReader::M4aAudioFileReader() :
    opened_(false)
{
}

//------------------------------------------------------------------------------

M4aAudioFileReader::~M4aAudioFileReader()
{
    if (opened_) {
        mp4read_close();
    }
}

//------------------------------------------------------------------------------

bool M4aAudioFileReader::open(const char* input_filename, bool /* show_info */)
{
    // mp4read_open() only reads through the given path (fopen(name, "rb")),
    // never modifies it — const_cast is safe despite the C API's non-const
    // char* parameter.
    if (mp4read_open(const_cast<char*>(input_filename)) != 0) {
        return false;
    }

    // A real M4A/AAC audio track has a non-empty AudioSpecificConfig
    // (needed by NeAACDecInit2 below) and at least one sample. Without
    // either, this is a file mp4read.c could parse as valid MP4 boxes but
    // that isn't actually AAC audio we can decode.
    if (mp4config.asc.size == 0 || mp4config.frame.nsamples == 0) {
        mp4read_close();
        return false;
    }

    opened_ = true;

    return true;
}

//------------------------------------------------------------------------------

bool M4aAudioFileReader::run(AudioProcessor& processor)
{
    if (!opened_) {
        return false;
    }

    NeAACDecHandle handle = NeAACDecOpen();

    NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(handle);
    config->outputFormat = FAAD_FMT_16BIT;
    NeAACDecSetConfiguration(handle, config);

    unsigned long sampleRate = 0;
    unsigned char channels = 0;

    const char initResult = NeAACDecInit2(
        handle, mp4config.asc.buf, mp4config.asc.size, &sampleRate, &channels);

    if (initResult < 0 || channels == 0) {
        NeAACDecClose(handle);
        return false;
    }

    if (!processor.init(static_cast<int>(sampleRate), channels, static_cast<long>(mp4config.samples), 0)) {
        NeAACDecClose(handle);
        return false;
    }

    bool success = true;

    while (mp4read_frame() == 0) {
        NeAACDecFrameInfo frameInfo;

        void* sampleBuffer = NeAACDecDecode(
            handle, &frameInfo, mp4config.bitbuf.data, mp4config.bitbuf.size);

        // A decode error on one frame is recoverable — skip it and keep
        // going, same philosophy as Mp3AudioFileReader's libmad loop
        // (MAD_RECOVERABLE frame-level errors don't abort the whole file).
        if (frameInfo.error != 0 || sampleBuffer == nullptr || frameInfo.samples == 0) {
            continue;
        }

        const int frameCount = static_cast<int>(frameInfo.samples / frameInfo.channels);

        if (!processor.process(static_cast<const short*>(sampleBuffer), frameCount)) {
            success = false;
            break;
        }

        if (!processor.shouldContinue()) {
            break;
        }
    }

    processor.done();

    NeAACDecClose(handle);

    return success;
}
