// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <magenta/types.h>
#include <mxtl/algorithm.h>
#include <mxtl/auto_call.h>
#include <stdio.h>
#include <string.h>

#include "sine-source.h"
#include "wav-sink.h"
#include "wav-source.h"

static constexpr float DEFAULT_PLUG_MONITOR_DURATION = 10.0f;
static constexpr float MIN_PLUG_MONITOR_DURATION = 0.5f;
static constexpr float DEFAULT_TONE_DURATION = 1.5f;
static constexpr float MIN_TONE_DURATION = 0.001f;
static constexpr float DEFAULT_TONE_FREQ = 440.0f;
static constexpr float MIN_TONE_FREQ = 15.0f;
static constexpr float MAX_TONE_FREQ = 20000.0f;
static constexpr float DEFAULT_RECORD_DURATION = 30.0f;
static constexpr uint32_t DEFAULT_FRAME_RATE = 48000;
static constexpr uint32_t DEFAULT_BITS_PER_SAMPLE = 16;
static constexpr uint32_t DEFAULT_CHANNELS = 2;
static constexpr audio_sample_format_t AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT =
    static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                       AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);

enum class Command {
    INVALID,
    INFO,
    MUTE,
    UNMUTE,
    GAIN,
    PLUG_MONITOR,
    TONE,
    PLAY,
    RECORD,
};

void usage(const char* prog_name) {
    printf("usage:\n");
    printf("%s [options] <cmd> <cmd params>\n", prog_name);
    printf("\nOptions\n");
    printf("  When options are specified, they must occur before the command and command\n"
           "  arguments.  Valid options include...\n"
           "  -d <device id>   : Dev node id for the audio device to use.  Defaults to 0.\n"
           "  -t <device type> : The type of device to open, either input or output.  Ignored if\n"
           "                     the command given is direction specific (play, record, etc).\n"
           "                     Otherwise, defaults to output.\n"
           "  -r <frame rate>  : Frame rate to use.  Defaults to 48000 Hz\n"
           "  -b <bits/sample> : Bits per sample to use.  Defaults to 16\n"
           "  -c <channels>    : Channels to use.  Defaults to 2\n");
    printf("\nValid command are\n");
    printf("info   : Fetches capability and status info for the specified stream\n");
    printf("mute   : Mute the specified stream\n");
    printf("unmute : Mute the specified stream\n");
    printf("gain   : Params : <db_gain>\n");
    printf("         Set the gain of the stream to the specified level\n");
    printf("pmon   : Params : [<duration>]\n"
           "         Monitor the plug state of the specified stream for the\n"
           "         specified amount of time.  Duration defaults to %.1fs and is\n"
           "         floored at %u mSec\n",
           DEFAULT_PLUG_MONITOR_DURATION,
           static_cast<int>(MIN_PLUG_MONITOR_DURATION * 1000));
    printf("tone   : Params : [<freq>] [<duration>]\n"
           "         Play a sinusoidal tone of the specified frequency for the\n"
           "         specified duration.  Frequency is clamped on the range\n"
           "         [%.1f, %.1f] Hz.  Duration is given in seconds and floored\n"
           "         at %d mSec.  Default is %.1f Hz for %.1f seconds\n",
            MIN_TONE_FREQ,
            MAX_TONE_FREQ,
            static_cast<int>(MIN_TONE_DURATION * 1000),
            DEFAULT_TONE_FREQ,
            DEFAULT_TONE_DURATION);
    printf("play   : Params : <file>\n");
    printf("         Play the specified WAV file on the selected output.\n");
    printf("record : Params : <file> [duration]\n"
           "         Record to the specified WAV file from the selected input.\n"
           "         Duration defaults to %.1f seconds if unspecified.\n",
           DEFAULT_RECORD_DURATION);
}

int main(int argc, const char** argv) {
    bool input = false;
    uint32_t dev_id = 0;
    uint32_t frame_rate = DEFAULT_FRAME_RATE;
    uint32_t bits_per_sample = DEFAULT_BITS_PER_SAMPLE;
    uint32_t channels = DEFAULT_CHANNELS;
    Command cmd = Command::INVALID;
    auto print_usage = mxtl::MakeAutoCall([prog_name = argv[0]]() { usage(prog_name); });
    int arg = 1;

    if (arg >= argc) return -1;

    struct {
        const char* name;
        const char* tag;
        uint32_t*   val;
    } UINT_OPTIONS[] = {
        { .name = "-d", .tag = "device ID",   .val = &dev_id },
        { .name = "-r", .tag = "frame rate",  .val = &frame_rate },
        { .name = "-b", .tag = "bits/sample", .val = &bits_per_sample },
        { .name = "-c", .tag = "channels",    .val = &channels },
    };

    static const struct {
        const char* name;
        Command cmd;
        bool force_out;
        bool force_in;
    } COMMANDS[] = {
        { "info",   Command::INFO,          false, false },
        { "mute",   Command::MUTE,          false, false },
        { "unmute", Command::UNMUTE,        false, false },
        { "gain",   Command::GAIN,          false, false },
        { "pmon",   Command::PLUG_MONITOR,  false, false },
        { "tone",   Command::TONE,          true,  false },
        { "play",   Command::PLAY,          true,  false },
        { "record", Command::RECORD,        false, true  },
    };

    while (arg < argc) {
        // Check to see if this is an integer option
        bool parsed_option = false;
        for (const auto& o : UINT_OPTIONS) {
            if (!strcmp(o.name, argv[arg])) {
                // Looks like this is an integer argument we care about.
                // Attempt to parse it.
                if (++arg >= argc) return -1;
                if (sscanf(argv[arg], "%u", o.val) != 1) {
                    printf("Failed to parse %s option, \"%s\"\n", o.tag, argv[arg]);
                    return -1;
                }
                ++arg;
                parsed_option = true;
                break;
            }
        }

        // If we successfully parse an integer option, continue on to the next
        // argument (if any).
        if (parsed_option)
            continue;

        // Was this the device type flag?
        if (!strcmp("-t", argv[arg])) {
            if (++arg >= argc) return -1;
            if (!strcmp("input", argv[arg])) {
                input = true;
            } else
            if (!strcmp("output", argv[arg])) {
                input = false;
            } else {
                printf("Invalid input/output specifier \"%s\".\n", argv[arg]);
                return -1;
            }
            ++arg;
            continue;
        }

        // Well, this didn't look like an option we understand, so it must be a
        // command.  Attempt to figure out what command it was.
        for (const auto& entry : COMMANDS) {
            if (!strcmp(entry.name, argv[arg])) {
                cmd = entry.cmd;
                parsed_option = true;
                arg++;

                if (entry.force_out) input = false;
                if (entry.force_in)  input = true;

                break;
            }
        }

        if (!parsed_option) {
            printf("Failed to parse command ID \"%s\"\n", argv[arg]);
            return -1;
        }

        break;
    }

    if (cmd == Command::INVALID) {
        printf("Failed to find valid command ID.\n");
        return -1;
    }

    audio_sample_format_t sample_format;
    switch (bits_per_sample) {
    case 8:  sample_format = AUDIO_SAMPLE_FORMAT_UNSIGNED_8BIT; break;
    case 16: sample_format = AUDIO_SAMPLE_FORMAT_16BIT; break;
    case 20: sample_format = AUDIO_SAMPLE_FORMAT_20BIT_IN32; break;
    case 24: sample_format = AUDIO_SAMPLE_FORMAT_24BIT_IN32; break;
    case 32: sample_format = AUDIO_SAMPLE_FORMAT_32BIT; break;
    default:
        printf("Unsupported number of bits per sample (%u)\n", bits_per_sample);
        return -1;
    }

    float tone_freq = 440.0;
    float duration;
    const char* wav_filename = nullptr;
    float target_gain = -100.0;

    // Parse any additional arguments
    switch (cmd) {
    case Command::GAIN:
        if (arg >= argc) return -1;
        if (sscanf(argv[arg], "%f", &target_gain) != 1) {
            printf("Failed to parse gain \"%s\"\n", argv[arg]);
            return -1;
        }
        arg++;
        break;

    case Command::PLUG_MONITOR:
        duration = DEFAULT_PLUG_MONITOR_DURATION;
        if (arg < argc) {
            if (sscanf(argv[arg], "%f", &duration) != 1) {
                printf("Failed to parse plug monitor duration \"%s\"\n", argv[arg]);
                return -1;
            }
            arg++;
            duration = mxtl::max(duration, MIN_PLUG_MONITOR_DURATION);
        }
        break;

    case Command::TONE:
        duration = DEFAULT_TONE_DURATION;
        if (arg < argc) {
            if (sscanf(argv[arg], "%f", &tone_freq) != 1) {
                printf("Failed to parse tone frequency \"%s\"\n", argv[arg]);
                return -1;
            }
            arg++;

            if (arg < argc) {
                if (sscanf(argv[arg], "%f", &duration) != 1) {
                    printf("Failed to parse tone duration \"%s\"\n", argv[arg]);
                    return -1;
                }
                arg++;
            }

            tone_freq = mxtl::clamp(tone_freq, 15.0f, 20000.0f);
            duration = mxtl::max(duration, MIN_TONE_DURATION);
        }
        break;

    case Command::PLAY:
    case Command::RECORD:
        if (arg >= argc) return -1;
        wav_filename = argv[arg];
        arg++;

        if (cmd == Command::RECORD) {
            duration = DEFAULT_RECORD_DURATION;
            if (arg < argc) {
                if (sscanf(argv[arg], "%f", &duration) != 1) {
                    printf("Failed to parse record duration \"%s\"\n", argv[arg]);
                    return -1;
                }
                arg++;
            }
        }

        break;

    default:
        break;
    }

    if (arg != argc) {
        printf("Invalid number of arguments.\n");
        return -1;
    }

    // Argument parsing is done, we can cancel the usage dump.
    print_usage.cancel();

    // Open the selected stream.
    mxtl::unique_ptr<audio::utils::AudioDeviceStream> stream;
    if (input) stream = audio::utils::AudioInput::Create(dev_id);
    else       stream = audio::utils::AudioOutput::Create(dev_id);
    if (stream == nullptr) {
        printf("Out of memory!\n");
        return MX_ERR_NO_MEMORY;
    }

    // No need to log in the case of failure.  Open has already done so.
    mx_status_t res = stream->Open();
    if (res != MX_OK)
        return res;

    // Execute the chosen command.
    switch (cmd) {
    case Command::INFO:         return stream->DumpInfo();
    case Command::MUTE:         return stream->SetMute(true);
    case Command::UNMUTE:       return stream->SetMute(false);
    case Command::GAIN:         return stream->SetGain(target_gain);
    case Command::PLUG_MONITOR: return stream->PlugMonitor(duration);

    case Command::TONE: {
        if (stream->input()) {
            printf("The \"tone\" command can only be used on output streams.\n");
            return -1;
        }

        SineSource sine_source;
        res = sine_source.Init(tone_freq, 1.0, duration, frame_rate, channels, sample_format);
        if (res != MX_OK) {
            printf("Failed to initialize sine wav generator (res %d)\n", res);
            return res;
        }

        printf("Playing %.2f Hz tone for %.2f seconds\n", tone_freq, duration);
        return static_cast<audio::utils::AudioOutput*>(stream.get())->Play(sine_source);
    }

    case Command::PLAY: {
        if (stream->input()) {
            printf("The \"play\" command can only be used on output streams.\n");
            return -1;
        }

        WAVSource wav_source;
        res = wav_source.Initialize(wav_filename);
        if (res != MX_OK)
            return res;

        return static_cast<audio::utils::AudioOutput*>(stream.get())->Play(wav_source);
    }

    case Command::RECORD: {
        if (!stream->input()) {
            printf("The \"record\" command can only be used on input streams.\n");
            return -1;
        }

        res = stream->SetFormat(frame_rate, static_cast<uint16_t>(channels), sample_format);
        if (res != MX_OK) {
            printf("Failed to set format (rate %u, chan %u, fmt 0x%08x, res %d)\n",
                    frame_rate, channels, sample_format, res);
            return -1;
        }

        WAVSink wav_sink;
        res = wav_sink.Initialize(wav_filename);
        if (res != MX_OK)
            return res;

        return static_cast<audio::utils::AudioInput*>(stream.get())->Record(wav_sink, duration);
    }

    default:
        MX_DEBUG_ASSERT(false);
        return -1;
    }
}
