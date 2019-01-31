// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define SAMSUNG_RPT_ID_TOUCH 1
#define SAMSUNG_RPT_ID_MOUSE 4

#define SAMSUNG_FINGER_ID_TSWITCH_MASK 0x01
#define SAMSUNG_FINGER_ID_CONTACT_MASK 0x7f
#define samsung_finger_id_tswitch(b) (b & SAMSUNG_FINGER_ID_TSWITCH_MASK)
#define samsung_finger_id_contact(b) ((b >> 1) & SAMSUNG_FINGER_ID_CONTACT_MASK)

#define SAMSUNG_X_MAX 32767
#define SAMSUNG_Y_MAX 32767

typedef struct samsung_finger {
    uint8_t finger_id;
    uint8_t width;
    uint8_t height;
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) samsung_finger_t;

typedef struct samsung_touch {
    uint8_t rpt_id;
    samsung_finger_t fingers[10];
    uint16_t scan_time;
    uint8_t contact_count;
} __attribute__((packed)) samsung_touch_t;

bool is_samsung_touch_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_samsung_touch(int fd);

__END_CDECLS

