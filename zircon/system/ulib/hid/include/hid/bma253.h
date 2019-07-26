// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

#define BMA253_RPT_ID_INPUT 1
#define BMA253_RPT_ID_FEATURE 2

typedef struct bma253_input_rpt {
  uint8_t rpt_id;
  uint16_t acceleration_x;
  uint16_t acceleration_y;
  uint16_t acceleration_z;
  uint8_t temperature;
} __PACKED bma253_input_rpt_t;

typedef struct bma253_feature_rpt {
  uint8_t rpt_id;
  uint32_t interval_ms;
} __PACKED bma253_feature_rpt_t;

size_t get_bma253_report_desc(const uint8_t** buf);

__END_CDECLS
