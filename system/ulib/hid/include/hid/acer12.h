// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

#define ACER12_RPT_DESC_LEN 660

#define ACER12_FINGER_ID_TSWITCH_MASK 0x01
#define ACER12_FINGER_ID_CONTACT_MASK 0xfc
#define acer12_finger_id_tswitch(b) (b & ACER12_FINGER_ID_TSWITCH_MASK)
#define acer12_finger_id_contact(b) ((b & ACER12_FINGER_ID_CONTACT_MASK) >> 2)

#define ACER12_X_MAX 3024
#define ACER12_Y_MAX 2064

typedef struct acer12_finger {
    uint8_t finger_id;
    uint8_t width;
    uint8_t height;
    // Both X and Y are repeated twice in every report.
    uint16_t x, xx;
    uint16_t y, yy;
} __attribute__((packed)) acer12_finger_t;

typedef struct acer12_touch {
    uint8_t rpt_id;
    acer12_finger_t fingers[5];
    uint32_t scan_time;
    uint8_t contact_count;  // will be zero for reports for fingers 6-10
} __attribute__((packed)) acer12_touch_t;

// Use this report descriptor to test whether an input device is an
// Acer12 touchscreen.
extern const uint8_t acer12_touch_report_desc[];

__END_CDECLS
