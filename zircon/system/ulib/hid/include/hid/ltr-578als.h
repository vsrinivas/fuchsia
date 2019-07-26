// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

#define LTR_578ALS_RPT_ID_INPUT 1
#define LTR_578ALS_RPT_ID_FEATURE 2

typedef struct ltr_578als_input_rpt {
  uint8_t rpt_id;
  uint32_t ambient_light;
  uint16_t proximity;
} __PACKED ltr_578als_input_rpt_t;

typedef struct ltr_578als_feature_rpt {
  uint8_t rpt_id;
  uint32_t interval_ms;
} __PACKED ltr_578als_feature_rpt_t;

size_t get_ltr_578als_report_desc(const uint8_t** buf);

__END_CDECLS
