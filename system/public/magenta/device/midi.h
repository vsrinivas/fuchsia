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
    MIDI_TYPE_SOURCE = 1,
    // Device type for MIDI sink
    MIDI_TYPE_SINK = 2,
};

// returns the device type (either MIDI_TYPE_SOURCE or MIDI_TYPE_SINK)
// call with out_len = sizeof(int)
#define IOCTL_MIDI_GET_DEVICE_TYPE      IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_MIDI, 0)

IOCTL_WRAPPER_OUT(ioctl_midi_get_device_type, IOCTL_MIDI_GET_DEVICE_TYPE, int);

__END_CDECLS
