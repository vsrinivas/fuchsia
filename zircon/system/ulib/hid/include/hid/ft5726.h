// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define FT5726_RPT_ID_TOUCH 1

#define FT5726_FINGER_ID_TSWITCH_MASK 0x01
#define FT5726_FINGER_ID_CONTACT_MASK 0xfc
#define ft5726_finger_id_tswitch(b) ((b)&FT5726_FINGER_ID_TSWITCH_MASK)
#define ft5726_finger_id_contact(b) (((b)&FT5726_FINGER_ID_CONTACT_MASK) >> 2)

#define FT5726_X_MAX 800
#define FT5726_Y_MAX 1280

typedef struct ft5726_finger {
  uint8_t finger_id;
  uint16_t x;
  uint16_t y;
} __PACKED ft5726_finger_t;

typedef struct ft5726_touch {
  uint8_t rpt_id;
  ft5726_finger_t fingers[5];
  uint8_t contact_count;  // will be zero for reports for fingers 6-10
} __PACKED ft5726_touch_t;

bool is_ft5726_touch_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_ft5726_touch(int fd);

size_t get_ft5726_report_desc(const uint8_t** buf);

__END_CDECLS
