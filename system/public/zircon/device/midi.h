// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

__BEGIN_CDECLS

enum {
    // Device type for MIDI source
    MIDI_TYPE_SOURCE = (1u << 0),
    // Device type for MIDI sink
    MIDI_TYPE_SINK   = (1u << 1),

    MIDI_TYPE_SINK_SOURCE = MIDI_TYPE_SINK | MIDI_TYPE_SOURCE,
};

// returns the device type (either MIDI_TYPE_SOURCE or MIDI_TYPE_SINK)
// call with out_len = sizeof(int)
#define IOCTL_MIDI_GET_DEVICE_TYPE      IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_MIDI, 0)

IOCTL_WRAPPER_OUT(ioctl_midi_get_device_type, IOCTL_MIDI_GET_DEVICE_TYPE, int);

__END_CDECLS
