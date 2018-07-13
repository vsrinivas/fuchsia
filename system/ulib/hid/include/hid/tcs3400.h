// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off
#define TCS3400_RPT_ID_POLL      1
#define TCS3400_RPT_ID_INTERRUPT 2
// clang-format on

typedef struct tcs3400_data {
    uint8_t rpt_id;
    uint16_t illuminance;
} __PACKED tcs3400_data_t;

size_t get_tcs3400_report_desc(const uint8_t** buf);

__END_CDECLS
