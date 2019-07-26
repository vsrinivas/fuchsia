// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define GT92XX_RPT_ID_TOUCH 1

typedef struct gt92xx_finger {
  uint8_t finger_id;
  uint16_t x;
  uint16_t y;
} __PACKED gt92xx_finger_t;

typedef struct gt92xx_touch {
  uint8_t rpt_id;
  gt92xx_finger_t fingers[5];
  uint8_t contact_count;  // will be zero for reports for fingers 6-10
} __PACKED gt92xx_touch_t;

bool is_gt92xx_touch_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_gt92xx_touch(int fd);

size_t get_gt92xx_report_desc(const uint8_t** buf);

__END_CDECLS
