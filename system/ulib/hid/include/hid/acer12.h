// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define ACER12_RPT_ID_TOUCH 1
#define ACER12_RPT_ID_STYLUS 7

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

#define ACER12_STYLUS_STATUS_INRANGE 0x01
#define ACER12_STYLUS_STATUS_TSWITCH 0x02
#define ACER12_STYLUS_STATUS_BARREL  0x04
#define ACER12_STYLUS_STATUS_INVERT  0x08
#define ACER12_STYLUS_STATUS_ERASER  0x10
#define acer12_stylus_status_inrange(b) (b & ACER12_STYLUS_STATUS_INRANGE)
#define acer12_stylus_status_tswitch(b) (b & ACER12_STYLUS_STATUS_TSWITCH)
#define acer12_stylus_status_barrel(b)  (b & ACER12_STYLUS_STATUS_BARREL)
#define acer12_stylus_status_invert(b)  (b & ACER12_STYLUS_STATUS_INVERT)
#define acer12_stylus_status_eraser(b)  (b & ACER12_STYLUS_STATUS_ERASER)

#define ACER12_STYLUS_X_MAX 4032
#define ACER12_STYLUS_Y_MAX 2752

typedef struct acer12_stylus {
    uint8_t rpt_id;
    uint8_t status;
    uint16_t x;
    uint16_t y;
    uint16_t pressure;
} __attribute__((packed)) acer12_stylus_t;

bool is_acer12_touch_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_acer12_touch(int fd);

__END_CDECLS
