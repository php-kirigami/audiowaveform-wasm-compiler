// M4A/AAC support (CLAUDE.md decision 9's correction #2). This file is
// genuinely new code written for this project, not extracted from
// audiowaveform (which has no MP4/AAC reader) — modeled on the shape of
// audiowaveform's own real Mp3AudioFileReader.cpp/.h (fetched from GitHub
// while designing this), implementing the same AudioFileReader interface
// audiowaveform's other readers do. Decodes via libfaad2's NeAACDec* API
// plus its own frontend/mp4read.c MP4 box demuxer (vendored, not
// audiowaveform's code either — see ../audiowaveform/Dockerfile).
//
// Licensed GPL-3.0-or-later, matching this project's own LICENSE.

#if !defined(INC_M4A_AUDIO_FILE_READER_H)
#define INC_M4A_AUDIO_FILE_READER_H

#include "AudioFileReader.h"

class AudioProcessor;

class M4aAudioFileReader : public AudioFileReader
{
    public:
        M4aAudioFileReader();
        virtual ~M4aAudioFileReader();

        M4aAudioFileReader(const M4aAudioFileReader&) = delete;
        M4aAudioFileReader& operator=(const M4aAudioFileReader&) = delete;

    public:
        // Note: mp4read.c holds its parsed-file state in process-wide
        // globals (a plain C module, not a class), not per-instance — fine
        // given this project's usage is always one synchronous decode at a
        // time (see binding.cpp's TEMP_INPUT_PATH comment), but this class
        // is not safe to use from two overlapping decodes.
        virtual bool open(const char* input_filename, bool show_info = true);

        virtual bool run(AudioProcessor& processor);

    private:
        bool opened_;
};

#endif // #if !defined(INC_M4A_AUDIO_FILE_READER_H)
