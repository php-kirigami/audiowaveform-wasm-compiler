// See WebmAudioFileReader.h for context.

#include "WebmAudioFileReader.h"
#include "AudioProcessor.h"

#include <nestegg/nestegg.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include <opus/opus.h>

#include <algorithm>
#include <vector>

//------------------------------------------------------------------------------

namespace {

int64_t nesteggIoRead(void* buffer, size_t length, void* userdata)
{
    FILE* file = static_cast<FILE*>(userdata);
    const size_t bytesRead = fread(buffer, 1, length, file);

    if (bytesRead == 0) {
        return feof(file) ? 0 : -1;
    }

    return static_cast<int64_t>(bytesRead);
}

int nesteggIoSeek(int64_t offset, int whence, void* userdata)
{
    FILE* file = static_cast<FILE*>(userdata);

    int origin;

    switch (whence) {
        case NESTEGG_SEEK_SET: origin = SEEK_SET; break;
        case NESTEGG_SEEK_CUR: origin = SEEK_CUR; break;
        case NESTEGG_SEEK_END: origin = SEEK_END; break;
        default: return -1;
    }

    return fseek(file, static_cast<long>(offset), origin) == 0 ? 0 : -1;
}

int64_t nesteggIoTell(void* userdata)
{
    FILE* file = static_cast<FILE*>(userdata);
    return ftell(file);
}

// Opus is always decoded at 48kHz internally, regardless of the source
// signal's original sample rate (RFC 7845) — nestegg_track_audio_params()'s
// reported rate isn't a valid opus_decoder_create() Fs value in general, so
// this is fixed, not read from the container.
const opus_int32 OPUS_DECODE_SAMPLE_RATE = 48000;

// Largest possible Opus frame (120ms at 48kHz) per channel — the safe
// upper bound opus_decode()'s own documentation recommends for the output
// buffer size, not a tuned minimum.
const int OPUS_MAX_FRAME_SAMPLES = 5760;

} // namespace

//------------------------------------------------------------------------------

WebmAudioFileReader::WebmAudioFileReader() :
    file_(nullptr),
    demux_(nullptr),
    track_(0),
    codec_(-1)
{
}

//------------------------------------------------------------------------------

WebmAudioFileReader::~WebmAudioFileReader()
{
    if (demux_ != nullptr) {
        nestegg_destroy(static_cast<nestegg*>(demux_));
    }

    if (file_ != nullptr) {
        fclose(file_);
    }
}

//------------------------------------------------------------------------------

bool WebmAudioFileReader::open(const char* input_filename, bool /* show_info */)
{
    file_ = fopen(input_filename, "rb");

    if (file_ == nullptr) {
        return false;
    }

    nestegg_io io;
    io.read = nesteggIoRead;
    io.seek = nesteggIoSeek;
    io.tell = nesteggIoTell;
    io.userdata = file_;

    nestegg* demux = nullptr;

    if (nestegg_init(&demux, io, nullptr, -1) != 0) {
        return false;
    }

    demux_ = demux;

    unsigned int trackCount = 0;

    if (nestegg_track_count(demux, &trackCount) != 0) {
        return false;
    }

    // Use the first Vorbis or Opus audio track found — matches this
    // project's existing single-track assumption (WaveformGenerator itself
    // only ever processes one merged/stereo signal, see binding.cpp's
    // split_channels comment).
    for (unsigned int track = 0; track < trackCount; ++track) {
        if (nestegg_track_type(demux, track) != NESTEGG_TRACK_AUDIO) {
            continue;
        }

        const int codec = nestegg_track_codec_id(demux, track);

        if (codec == NESTEGG_CODEC_VORBIS || codec == NESTEGG_CODEC_OPUS) {
            track_ = track;
            codec_ = codec;
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------

bool WebmAudioFileReader::run(AudioProcessor& processor)
{
    if (demux_ == nullptr || codec_ < 0) {
        return false;
    }

    if (codec_ == NESTEGG_CODEC_OPUS) {
        return runOpus(processor);
    }

    return runVorbis(processor);
}

//------------------------------------------------------------------------------

bool WebmAudioFileReader::runVorbis(AudioProcessor& processor)
{
    nestegg* demux = static_cast<nestegg*>(demux_);

    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;

    vorbis_info_init(&info);
    vorbis_comment_init(&comment);

    // A Vorbis track's CodecPrivate is the three standard Vorbis header
    // packets (identification/comment/setup), which nestegg already splits
    // into separate items for us — no manual Xiph-lacing parsing needed.
    unsigned int headerCount = 0;
    nestegg_track_codec_data_count(demux, track_, &headerCount);

    bool headersOk = headerCount > 0;

    for (unsigned int i = 0; i < headerCount && headersOk; ++i) {
        unsigned char* data = nullptr;
        size_t length = 0;

        if (nestegg_track_codec_data(demux, track_, i, &data, &length) != 0) {
            headersOk = false;
            break;
        }

        ogg_packet packet = {};
        packet.packet = data;
        packet.bytes = static_cast<long>(length);
        packet.b_o_s = (i == 0) ? 1 : 0;
        packet.packetno = i;

        if (vorbis_synthesis_headerin(&info, &comment, &packet) != 0) {
            headersOk = false;
        }
    }

    if (!headersOk || vorbis_synthesis_init(&dsp, &info) != 0) {
        vorbis_comment_clear(&comment);
        vorbis_info_clear(&info);
        return false;
    }

    vorbis_block_init(&dsp, &block);

    bool success = processor.init(static_cast<int>(info.rate), info.channels, 0, 0);

    std::vector<short> outputBuffer;

    nestegg_packet* packet = nullptr;

    while (success && nestegg_read_packet(demux, &packet) > 0) {
        unsigned int packetTrack = 0;
        nestegg_packet_track(packet, &packetTrack);

        if (packetTrack != track_) {
            nestegg_free_packet(packet);
            continue;
        }

        unsigned int chunks = 0;
        nestegg_packet_count(packet, &chunks);

        for (unsigned int chunk = 0; chunk < chunks && success; ++chunk) {
            unsigned char* data = nullptr;
            size_t length = 0;
            nestegg_packet_data(packet, chunk, &data, &length);

            ogg_packet oggPacket = {};
            oggPacket.packet = data;
            oggPacket.bytes = static_cast<long>(length);

            if (vorbis_synthesis(&block, &oggPacket) != 0) {
                continue;
            }

            vorbis_synthesis_blockin(&dsp, &block);

            float** pcm = nullptr;
            int samples;

            while (success && (samples = vorbis_synthesis_pcmout(&dsp, &pcm)) > 0) {
                outputBuffer.resize(static_cast<size_t>(samples) * info.channels);

                for (int s = 0; s < samples; ++s) {
                    for (int channel = 0; channel < info.channels; ++channel) {
                        const float clamped = std::max(-1.0f, std::min(1.0f, pcm[channel][s]));
                        outputBuffer[s * info.channels + channel] =
                            static_cast<short>(clamped * 32767.0f);
                    }
                }

                success = processor.process(outputBuffer.data(), samples);

                vorbis_synthesis_read(&dsp, samples);
            }
        }

        nestegg_free_packet(packet);
    }

    processor.done();

    vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp);
    vorbis_comment_clear(&comment);
    vorbis_info_clear(&info);

    return success;
}

//------------------------------------------------------------------------------

bool WebmAudioFileReader::runOpus(AudioProcessor& processor)
{
    nestegg* demux = static_cast<nestegg*>(demux_);

    nestegg_audio_params params = {};

    if (nestegg_track_audio_params(demux, track_, &params) != 0 || params.channels == 0) {
        return false;
    }

    const int channels = static_cast<int>(params.channels);

    int error = OPUS_OK;
    OpusDecoder* decoder = opus_decoder_create(OPUS_DECODE_SAMPLE_RATE, channels, &error);

    if (error != OPUS_OK || decoder == nullptr) {
        return false;
    }

    bool success = processor.init(OPUS_DECODE_SAMPLE_RATE, channels, 0, 0);

    std::vector<short> outputBuffer(static_cast<size_t>(OPUS_MAX_FRAME_SAMPLES) * channels);

    nestegg_packet* packet = nullptr;

    while (success && nestegg_read_packet(demux, &packet) > 0) {
        unsigned int packetTrack = 0;
        nestegg_packet_track(packet, &packetTrack);

        if (packetTrack != track_) {
            nestegg_free_packet(packet);
            continue;
        }

        unsigned int chunks = 0;
        nestegg_packet_count(packet, &chunks);

        for (unsigned int chunk = 0; chunk < chunks && success; ++chunk) {
            unsigned char* data = nullptr;
            size_t length = 0;
            nestegg_packet_data(packet, chunk, &data, &length);

            const int frameSamples = opus_decode(
                decoder,
                data,
                static_cast<opus_int32>(length),
                outputBuffer.data(),
                OPUS_MAX_FRAME_SAMPLES,
                0
            );

            // A single corrupt packet is recoverable — skip it and keep
            // decoding, same philosophy as the other readers' frame-level
            // error handling.
            if (frameSamples <= 0) {
                continue;
            }

            success = processor.process(outputBuffer.data(), frameSamples);
        }

        nestegg_free_packet(packet);
    }

    processor.done();

    opus_decoder_destroy(decoder);

    return success;
}
