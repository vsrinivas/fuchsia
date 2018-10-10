// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off
#define BUTTONS_RPT_ID_INPUT       0x01
// clang-format on

typedef struct buttons_input_rpt {
    uint8_t rpt_id;
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint8_t volume : 2;
    uint8_t padding : 6;
#else
    uint8_t padding : 6;
    uint8_t volume : 2;
#endif
} __PACKED buttons_input_rpt_t;

size_t get_buttons_report_desc(const uint8_t** buf);

__END_CDECLS
