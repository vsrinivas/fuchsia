// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define EYOYO_RPT_ID_TOUCH 1

#define EYOYO_FINGER_ID_TSWITCH_MASK 0x01
#define EYOYO_FINGER_ID_CONTACT_MASK 0x7f
#define eyoyo_finger_id_tswitch(b) (b & EYOYO_FINGER_ID_TSWITCH_MASK)
#define eyoyo_finger_id_contact(b) ((b >> 1) & EYOYO_FINGER_ID_CONTACT_MASK)

#define EYOYO_X_MAX 32767
#define EYOYO_Y_MAX 32767

typedef struct eyoyo_finger {
    uint8_t finger_id;
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) eyoyo_finger_t;

typedef struct eyoyo_touch {
    uint8_t rpt_id;
    eyoyo_finger_t fingers[10];
    uint8_t unknown0;
    uint16_t unknown1;
} __attribute__((packed)) eyoyo_touch_t;

bool is_eyoyo_touch_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_eyoyo_touch(int fd);

__END_CDECLS
