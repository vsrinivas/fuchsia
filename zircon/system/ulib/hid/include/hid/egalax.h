// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define EGALAX_RPT_ID_TOUCH 1
#define EGALAX_X_MAX 4095
#define EGALAX_Y_MAX 4095

// size in bits of the button portion of the button_pad field
#define EGALAX_BTN_SZ 2
#define EGALAX_PRESSED_FLAGS_MASK 0x03
#define egalax_pressed_flags(b) ((b) & EGALAX_PRESSED_FLAGS_MASK)
#define egalax_pad(b) ((b) >> EGALAX_BTN_SZ)


typedef struct egalax_touchscreen {
    uint8_t report_id;
    // the lower two bits should be the active button and the upper 6 bits
    // is padding (or a value of unknown significance)
    uint8_t button_pad;
    uint16_t x;
    uint16_t y;
} __PACKED egalax_touch_t;

bool is_egalax_touchscreen_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_egalax_touchscreen(int fd);

__END_CDECLS
