// WebM audio support (2026-09-12, decision 19), requested by the user
// right after FLAC/Ogg/Opus/M4A landed. Like M4aAudioFileReader.h, this
// file is genuinely new code written for this project, not extracted from
// audiowaveform (which has no WebM reader) — implementing the same
// AudioFileReader interface audiowaveform's other readers do. Demuxes via
// Mozilla's nestegg (vendored, ISC-licensed), then decodes the demuxed
// packets with libvorbis/libopus directly (both already vendored for Ogg
// Vorbis/Opus support) — nestegg only demultiplexes, it doesn't decode.
//
// Scope: Vorbis and Opus audio tracks only, matching what "WebM" (as
// opposed to the more permissive Matroska/.mkv container format) actually
// restricts audio to.
//
// Licensed GPL-3.0-or-later, matching this project's own LICENSE.

#if !defined(INC_WEBM_AUDIO_FILE_READER_H)
#define INC_WEBM_AUDIO_FILE_READER_H

#include "AudioFileReader.h"

#include <cstdio>

class AudioProcessor;

class WebmAudioFileReader : public AudioFileReader
{
    public:
        WebmAudioFileReader();
        virtual ~WebmAudioFileReader();

        WebmAudioFileReader(const WebmAudioFileReader&) = delete;
        WebmAudioFileReader& operator=(const WebmAudioFileReader&) = delete;

    public:
        virtual bool open(const char* input_filename, bool show_info = true);

        virtual bool run(AudioProcessor& processor);

    private:
        bool runVorbis(AudioProcessor& processor);
        bool runOpus(AudioProcessor& processor);

    private:
        FILE* file_;
        void* demux_; // nestegg*, kept as void* so this header doesn't need <nestegg/nestegg.h>
        unsigned int track_;
        int codec_;
};

#endif // #if !defined(INC_WEBM_AUDIO_FILE_READER_H)
