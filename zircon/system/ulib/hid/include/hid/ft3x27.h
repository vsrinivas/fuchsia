// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define FT3X27_RPT_ID_TOUCH 1

#define FT3X27_FINGER_ID_TSWITCH_MASK 0x01
#define FT3X27_FINGER_ID_CONTACT_MASK 0xfc
#define ft3x27_finger_id_tswitch(b) ((b)&FT3X27_FINGER_ID_TSWITCH_MASK)
#define ft3x27_finger_id_contact(b) (((b)&FT3X27_FINGER_ID_CONTACT_MASK) >> 2)

#define FT3X27_X_MAX 600
#define FT3X27_Y_MAX 1024

typedef struct ft3x27_finger {
  uint8_t finger_id;
  uint16_t x;
  uint16_t y;
} __PACKED ft3x27_finger_t;

typedef struct ft3x27_touch {
  uint8_t rpt_id;
  ft3x27_finger_t fingers[5];
  uint8_t contact_count;  // will be zero for reports for fingers 6-10
} __PACKED ft3x27_touch_t;

bool is_ft3x27_touch_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_ft3x27_touch(int fd);

size_t get_ft3x27_report_desc(const uint8_t** buf);

__END_CDECLS
