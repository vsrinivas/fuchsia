// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off
#define AMBIENT_LIGHT_RPT_ID_INPUT       0x01
#define AMBIENT_LIGHT_RPT_ID_FEATURE     0x02
// clang-format on

typedef struct ambient_light_input_rpt {
    uint8_t rpt_id;
    uint8_t state;
    uint8_t event;
    uint16_t illuminance;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
} __PACKED ambient_light_input_rpt_t;

typedef struct ambient_light_feature_rpt {
    uint8_t rpt_id;
    uint8_t state;
    uint32_t interval_ms; // default (0) means no polling
    uint16_t threshold_low;
    uint16_t threshold_high;
} __PACKED ambient_light_feature_rpt_t;

size_t get_ambient_light_report_desc(const uint8_t** buf);

__END_CDECLS
