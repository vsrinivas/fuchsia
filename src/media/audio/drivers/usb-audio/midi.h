// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_MIDI_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_MIDI_H_

#include <zircon/types.h>

int get_midi_message_length(uint8_t status_byte);

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_MIDI_H_
