// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/types.h>
#include <mxtl/algorithm.h>
#include <mxtl/auto_call.h>
#include <stdio.h>
#include <string.h>

#include "audio-source.h"
#include "audio-output.h"
#include "sine-source.h"
#include "wav-source.h"

enum class Command {
    INVALID,
    INFO,
    MUTE,
    UNMUTE,
    GAIN,
    TONE,
    PLAY,
};

void usage(const char* prog_name) {
    printf("usage:\n");
    printf("%s [-d <device specifier>] <cmd> <cmd params>\n", prog_name);
    printf("\nDevice specifier\n");
    printf("  Device specifiers are optional, but must occur before the command\n");
    printf("  when supplied.  Parameters for devices specifiers take the form\n");
    printf("  <input/output> <dev_num>.  If no device specifier is provided,\n");
    printf("  output #0 will be chosen by default.\n");
    printf("\nValid command are\n");
    printf("info   : Fetches capability and status info for the specified stream\n");
    printf("mute   : Mute the specified stream\n");
    printf("unmute : Mute the specified stream\n");
    printf("gain   : Params : <db_gain>\n");
    printf("         Set the gain of the stream to the specified level\n");
    printf("tone   : Params : [<freq>] [<duration>]\n");
    printf("         Play a sinusoidal tone of the specified frequency for the\n");
    printf("         specified duration.  Frequency is clamped on the range\n");
    printf("         [15, 20000] Hz.  Duration is given in seconds and floored\n");
    printf("         at 1mSec.  Default is 440 Hz for 1.5 seconds\n");
    printf("play   : Params : <file>\n");
    printf("         Play the specified WAV file on the selected output.\n");
}

int main(int argc, const char** argv) {
    bool input = false;
    uint32_t dev_num = 0;
    Command cmd = Command::INVALID;
    auto print_usage = mxtl::MakeAutoCall([prog_name = argv[0]]() { usage(prog_name); });
    int arg = 1;

    if (arg >= argc) return -1;
    if (!strcmp("-d", argv[arg])) {
        // Parse the input/output specifier.
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

        // Parse the device ID specifier.
        if (++arg >= argc) return -1;
        if (sscanf(argv[arg], "%u", &dev_num) != 1) {
            printf("Failed to parse argument ID \"%s\"\n", argv[arg]);
            return -1;
        }

        // Move on to the command.
        if (++arg >= argc) return -1;
    }

    // Parse the command
    static const struct {
        const char* name;
        Command cmd;
    } COMMANDS[] = {
        { "info",   Command::INFO },
        { "mute",   Command::MUTE },
        { "unmute", Command::UNMUTE },
        { "gain",   Command::GAIN },
        { "tone",   Command::TONE },
        { "play",   Command::PLAY },
    };

    for (const auto& entry : COMMANDS) {
        if (!strcmp(entry.name, argv[arg])) {
            cmd = entry.cmd;
            break;
        }
    }

    if (cmd == Command::INVALID) {
        printf("Failed to parse command ID \"%s\"\n", argv[arg]);
        return -1;
    }

    arg++;

    float tone_freq = 440.0;
    float tone_duration = 1.5;
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

    case Command::TONE:
        if (arg < argc) {
            if (sscanf(argv[arg], "%f", &tone_freq) != 1) {
                printf("Failed to parse tone frequency \"%s\"\n", argv[arg]);
                return -1;
            }
            arg++;

            if (arg < argc) {
                if (sscanf(argv[arg], "%f", &tone_duration) != 1) {
                    printf("Failed to parse tone duration \"%s\"\n", argv[arg]);
                    return -1;
                }
                arg++;
            }

            tone_freq = mxtl::clamp(tone_freq, 15.0f, 20000.0f);
            tone_duration = mxtl::max(tone_duration, 0.001f);
        }
        break;

    case Command::PLAY:
        if (arg >= argc) return -1;
        wav_filename = argv[arg];
        arg++;
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
    auto stream = AudioStream::Create(input, dev_num);
    if (stream == nullptr) {
        printf("Out of memory!\n");
        return ERR_NO_MEMORY;
    }

    // No need to log in the case of failure.  Open has already done so.
    mx_status_t res = stream->Open();
    if (res != NO_ERROR)
        return res;

    // Execute the chosen command.
    switch (cmd) {
    case Command::INFO:   return stream->DumpInfo();
    case Command::MUTE:   return stream->SetMute(true);
    case Command::UNMUTE: return stream->SetMute(false);
    case Command::GAIN:   return stream->SetGain(target_gain);

    case Command::TONE: {
        if (stream->input()) {
            printf("The \"tone\" command can only be used on output streams.\n");
            return -1;
        }

        SineSource sine_source(tone_freq, 1.0, tone_duration);
        printf("Playing %.2f Hz tone for %.2f seconds\n", tone_freq, tone_duration);
        return static_cast<AudioOutput*>(stream.get())->Play(sine_source);
    }

    case Command::PLAY: {
        if (stream->input()) {
            printf("The \"play\" command can only be used on output streams.\n");
            return -1;
        }

        WAVSource wav_source;
        res = wav_source.Initialize(wav_filename);
        if (res != NO_ERROR)
            return res;

        return static_cast<AudioOutput*>(stream.get())->Play(wav_source);
    }

    default:
        MX_DEBUG_ASSERT(false);
        return -1;
    }
}
