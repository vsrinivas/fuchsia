// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>
#include <assert.h>
#include <stdint.h>

#include "midi.h"

// Number of bytes in a message nc from 8c to Ec
static const int CHANNEL_BYTE_LENGTHS[] = { 3, 3, 3, 3, 2, 2, 3 };

// Number of bytes in a message Fn from F0 to FF */
static const int SYSTEM_BYTE_LENGTHS[] = { 1, 2, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

int get_midi_message_length(uint8_t status_byte) {
    if (status_byte >= 0xF0) {
        // System messages use low nibble for size.
        static_assert(countof(SYSTEM_BYTE_LENGTHS) >= 16, "SYSTEM_BYTE_LENGTHS too small");
        return SYSTEM_BYTE_LENGTHS[status_byte & 0x0F];
    } else if(status_byte >= 0x80) {
        // Channel voice messages use high nibble for size.
        static_assert(countof(CHANNEL_BYTE_LENGTHS) >= 0xE - 8, "CHANNEL_BYTE_LENGTHS too small");
        return CHANNEL_BYTE_LENGTHS[(status_byte >> 4) - 8];
    } else {
        return 0; // data byte
    }
}
