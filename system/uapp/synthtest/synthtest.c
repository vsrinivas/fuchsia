// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/midi.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/types.h>

#include "midi.h"

#define DEV_MIDI    "/dev/class/midi"

#define MAX_AMPLITUDE 32767.0f

#define ATTACK_RAMP_SAMPLES 500
#define DECAY_RAMP_SAMPLES 500

typedef struct {
    double amplitude;    // between 0 and MAX_AMPLITUDE
    double frequency;
    uint8_t midi_note;
    int attack_samples;
    int decay_samples;
    bool active;
} audio_channel_t;

#define CHANNEL_COUNT 10
static audio_channel_t channels[CHANNEL_COUNT];
static mtx_t mutex = MTX_INIT;

static volatile bool midi_done = false;

static double midi_note_frequencies[128];

// compute frequencies for all possible notes
static void init_midi_note_frequencies(void) {
    // pow() can't handle negative exponents so we have to do this in two parts
    for (size_t i = 0; i < MIDI_REF_INDEX; i++) {
        midi_note_frequencies[i] = MIDI_REF_FREQUENCY / pow(2.0, (MIDI_REF_INDEX - i) / 12.0);
    }
    for (size_t i = MIDI_REF_INDEX; i < countof(midi_note_frequencies); i++) {
        midi_note_frequencies[i] = MIDI_REF_FREQUENCY * pow(2.0, (i - MIDI_REF_INDEX) / 12.0);
    }
}

static int midi_read_thread(void* arg) {
    int fd = (int)(uintptr_t)arg;
    uint8_t buffer[3];

    while (1) {
        int event_size = read(fd, buffer, sizeof(buffer));
        if (event_size < 1) {
            midi_done = true;
            break;
        }
        printf("MIDI event:");
        for (int i = 0; i < event_size; i++) {
            printf(" %02X", buffer[i]);
        }
        printf("\n");
        int command = buffer[0] & MIDI_COMMAND_MASK;

        mtx_lock(&mutex);

        if (command == MIDI_NOTE_OFF) {
            uint8_t note = buffer[1];
            for (int i = 0; i < CHANNEL_COUNT; i++) {
                if (channels[i].active && channels[i].midi_note == note) {
                    // start decay
                    channels[i].decay_samples = DECAY_RAMP_SAMPLES;
                }
            }
         } else if (command == MIDI_NOTE_ON) {
            uint8_t note = buffer[1];

            if (note < countof(midi_note_frequencies)) {
//            int velocity = buffer[2];

                // find a free channel
                for (int i = 0; i < CHANNEL_COUNT; i++) {
                    if (channels[i].active == false) {
                        channels[i].midi_note = note;
                        channels[i].frequency = midi_note_frequencies[note];
    //                    channels[i].amplitude = (MAX_AMPLITUDE * velocity) / 256;
                        channels[i].amplitude = MAX_AMPLITUDE / 6.0;
                        // start attack
                        channels[i].attack_samples = ATTACK_RAMP_SAMPLES;
                        channels[i].active = true;
                        break;
                    }
                }
            }
        }

        mtx_unlock(&mutex);
    }
    return 0;
}

static void synth_loop(int fd, int sample_rate) {

#define BUFFER_FRAMES 200
    int16_t buffer[BUFFER_FRAMES * 2];
    int16_t* buf_ptr = buffer;
    int frame = 0;

    for (uint64_t sample = 0; !midi_done; sample++) {
        mtx_lock(&mutex);

        double v = 0.0;
        double period = ((double)sample * (2.0 * M_PI)) / (double)sample_rate;

        for (int j = 0; j < CHANNEL_COUNT; j++) {
            audio_channel_t* channel = &channels[j];
            if (!channel->active) continue;

            double amplitude = channel->amplitude;
            if (channel->attack_samples > 0) {
                int attack = channel->attack_samples--;
                amplitude = (amplitude * (ATTACK_RAMP_SAMPLES - attack)) / ATTACK_RAMP_SAMPLES;
            } else if (channel->decay_samples > 0) {
                int decay = channel->decay_samples--;
                amplitude = (amplitude * decay) / DECAY_RAMP_SAMPLES;
                if (decay == 1) {
                    channel->active = false;
                }
            }
            v += sin(period * channel->frequency) * amplitude;
            // add some harmonics
            v += sin(period * channel->frequency * 2.0) * (amplitude / 3.0);
            v += sin(period * channel->frequency * 4.0) * (amplitude / 5.0);
        }
        mtx_unlock(&mutex);

        int16_t s = (int16_t)v;
        *buf_ptr++ = s;
        *buf_ptr++ = s;
        frame++;

        if (frame == BUFFER_FRAMES) {
            if (write(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
                // audio disconnected?
                return;
            }
            frame = 0;
            buf_ptr = buffer;
        }
    }
}

static int open_source(void) {
    struct dirent* de;
    DIR* dir = opendir(DEV_MIDI);
    if (!dir) {
        printf("Error opening %s\n", DEV_MIDI);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
       char devname[128];

        snprintf(devname, sizeof(devname), "%s/%s", DEV_MIDI, de->d_name);
        int fd = open(devname, O_RDWR);
        if (fd < 0) {
            printf("Error opening %s\n", devname);
            continue;
        }

        int device_type;
        int ret = ioctl_midi_get_device_type(fd, &device_type);
        if (ret != sizeof(device_type)) {
            printf("ioctl_midi_get_device_type failed for %s\n", devname);
            close(fd);
            continue;
        }
        if (device_type == MIDI_TYPE_SOURCE) {
            closedir(dir);
            return fd;
        } else {
            close(fd);
            continue;
        }
    }

    closedir(dir);
    return -1;
}

static int open_sink(uint32_t* out_sample_rate) {
    // TODO(johngro) : Fix this.  This code used to interface with the old
    // driver interface which has since been removed.  Either wire it directly
    // to the driver level using the audio-utils library, or move the example up
    // to drivers/audio in Fuchsia and have it interface to the system-wide
    // mixer.
#if 0
    struct dirent* de;
    DIR* dir = opendir(DEV_AUDIO);
    if (!dir) {
        printf("Error opening %s\n", DEV_AUDIO);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
       char devname[128];

        snprintf(devname, sizeof(devname), "%s/%s", DEV_AUDIO, de->d_name);
        int fd = open(devname, O_RDWR);
        if (fd < 0) {
            printf("Error opening %s\n", devname);
            continue;
        }

        int device_type;
        int ret = ioctl_audio_get_device_type(fd, &device_type);
        if (ret != sizeof(device_type)) {
            printf("ioctl_audio_get_device_type failed for %s\n", devname);
            goto next;
        }
        if (device_type != AUDIO_TYPE_SINK) {
            goto next;
        }

        // find the best sample rate
        int sample_rate_count;
        ret = ioctl_audio_get_sample_rate_count(fd, &sample_rate_count);
        if (ret != sizeof(sample_rate_count) || sample_rate_count <= 0) {
            printf("ioctl_audio_get_sample_rate_count failed\n");
            goto next;
        }
        int buffer_size = sample_rate_count * sizeof(uint32_t);
        uint32_t* sample_rates = malloc(buffer_size);
        if (!sample_rates) {
            printf("could not allocate for %d sample rate buffer\n", sample_rate_count);
            goto next;
        }
        ret = ioctl_audio_get_sample_rates(fd, sample_rates, buffer_size);
        if (ret != buffer_size) {
            printf("ioctl_audio_get_sample_rates failed\n");
            free(sample_rates);
            goto next;
        }
        uint32_t sample_rate = 0;
        for (int i = 0; i < sample_rate_count; i++) {
            if (sample_rates[i] > sample_rate) {
                sample_rate = sample_rates[i];
            }
        }
        ret = ioctl_audio_set_sample_rate(fd, &sample_rate);
        if (ret != MX_OK) {
            printf("%s ioctl_audio_set_sample_rate failed for %d\n", devname, sample_rate);
            goto next;
        }

        ioctl_audio_start(fd);

        printf("sample rate: %d\n", sample_rate);
        *out_sample_rate = sample_rate;
        closedir(dir);
        return fd;

next:
        close(fd);
    }

    closedir(dir);
#endif
    return -1;

}

int main(int argc, char **argv) {
    int src_fd = open_source();
    if (src_fd < 0) {
        printf("couldn't find a usable MIDI source\n");
        return -1;
    }

    uint32_t sample_rate;
    int dest_fd = open_sink(&sample_rate);
    if (dest_fd < 0) {
        close(src_fd);
        printf("couldn't find a usable audio sink\n");
        return -1;
    }

    memset(channels, 0, sizeof(channels));
    init_midi_note_frequencies();

    thrd_t thread;
    thrd_create_with_name(&thread, midi_read_thread, (void *)(uintptr_t)src_fd, "midi_read_thread");
    thrd_detach(thread);

    synth_loop(dest_fd, sample_rate);

    return 0;
}
