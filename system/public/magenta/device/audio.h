// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

__BEGIN_CDECLS

enum {
    // Device type for MIDI source
    AUDIO_TYPE_SOURCE = 1,
    // Device type for MIDI sink
    AUDIO_TYPE_SINK = 2,
};

// returns the device type (either AUDIO_TYPE_SOURCE or AUDIO_TYPE_SINK)
// call with out_len = sizeof(int)
#define IOCTL_AUDIO_GET_DEVICE_TYPE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 0)

// returns the number of supported sample rates
// call with out_len = sizeof(int)
#define IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT   IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 1)

// returns the list of supported sample rates
// call with out_buf pointing to array of uint32_t and
// out_len = <value returned from IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT> * sizeof(uint32_t)
#define IOCTL_AUDIO_GET_SAMPLE_RATES        IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 2)

// gets the current sample rate
// call with out_len = sizeof(uint32_t)
#define IOCTL_AUDIO_GET_SAMPLE_RATE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 3)

// sets the current sample rate
// call with in_len = sizeof(uint32_t)
#define IOCTL_AUDIO_SET_SAMPLE_RATE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 4)

// starts reading or writing audio data
// called with no arguments
#define IOCTL_AUDIO_START                   IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 5)

// stops reading or writing audio data
// called with no arguments
#define IOCTL_AUDIO_STOP                    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_AUDIO, 7)

IOCTL_WRAPPER_OUT(ioctl_audio_get_device_type, IOCTL_AUDIO_GET_DEVICE_TYPE, int);
IOCTL_WRAPPER_OUT(ioctl_audio_get_sample_rate_count, IOCTL_AUDIO_GET_SAMPLE_RATE_COUNT, int);
IOCTL_WRAPPER_VAROUT(ioctl_audio_get_sample_rates, IOCTL_AUDIO_GET_SAMPLE_RATES, uint32_t);
IOCTL_WRAPPER_OUT(ioctl_audio_get_sample_rate, IOCTL_AUDIO_GET_SAMPLE_RATE, uint32_t);
IOCTL_WRAPPER_IN(ioctl_audio_set_sample_rate, IOCTL_AUDIO_SET_SAMPLE_RATE, uint32_t);
IOCTL_WRAPPER(ioctl_audio_start, IOCTL_AUDIO_START);
IOCTL_WRAPPER(ioctl_audio_stop, IOCTL_AUDIO_STOP);

__END_CDECLS
