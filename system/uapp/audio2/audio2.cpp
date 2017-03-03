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

void usage(const char* prog_name) {
    printf("usage:\n");
    printf("%s [-d <dev_num>] [-t <freq> <duration>] [-w <file>]\n", prog_name);
    printf("  -d : specify the output stream number to open.  Defaults to 0\n");
    printf("  -t : specify the frequency and duration of a tone to play.\n");
    printf("       Frequency is clamped on the range [15, 20000] Hz\n");
    printf("       Duration is given in seconds and floored at 1mSec\n");
    printf("       Default is 440 Hz for 1.5 seconds\n");
    printf("  -w : specify the name of a WAV file to play instead of generating a tone\n");
}

int main(int argc, const char** argv) {
    uint32_t dev = 0;
    float tone_freq = 440.0;
    float tone_duration = 1.5;
    const char* wav_filename = nullptr;
    auto print_usage = mxtl::MakeAutoCall([&]() { usage(argv[0]); });

    for (int i = 1; i < argc; ++i) {
        if (!strcmp("-d", argv[i])) {
            if (((i + 1) >= argc) || (sscanf(argv[i + 1], "%u", &dev) != 1))
                return -1;
            i += 1;
        } else
        if (!strcmp("-t", argv[i])) {
            if (((i + 2) >= argc) ||
                (sscanf(argv[i + 1], "%f", &tone_freq) != 1) ||
                (sscanf(argv[i + 2], "%f", &tone_duration) != 1))
                return -1;
            i += 2;

            tone_freq = mxtl::clamp(tone_freq, 15.0f, 20000.0f);
            tone_duration = mxtl::max(tone_duration, 0.001f);
        } else
        if (!strcmp("-w", argv[i])) {
            if ((i + 1) >= argc)
                return -1;
            wav_filename = argv[i + 1];
            i += 1;

        } else {
            return -1;
        }
    }
    print_usage.cancel();

    SineSource   sine_source(tone_freq, 1.0, tone_duration);
    WAVSource    wav_source;
    AudioSource* source;
    mx_status_t  res;

    if (wav_filename == nullptr) {
        printf("Playing %.2f Hz tone for %.2f seconds\n", tone_freq, tone_duration);
        source = &sine_source;
    } else {
        res = wav_source.Initialize(wav_filename);
        if (res != NO_ERROR)
            return res;
        source = &wav_source;
    }

    AudioOutput output;
    {
        char stream_name[128];
        snprintf(stream_name, sizeof(stream_name), "/dev/class/audio2-output/%03u", dev);
        res = output.Open(stream_name);
        if (res != NO_ERROR)
            return res;
    }

    return output.Play(*source);
}
