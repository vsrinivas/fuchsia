// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off
#define AMBIENT_LIGHT_RPT_ID_SIMPLE_POLL      0x01
#define AMBIENT_LIGHT_RPT_ID_SIMPLE_INTERRUPT 0x02
// clang-format on

typedef struct ambient_light_data {
    uint8_t rpt_id;
    uint16_t illuminance;
} __PACKED ambient_light_data_t;

size_t get_ambient_light_report_desc(const uint8_t** buf);

__END_CDECLS
