// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off
#define BUTTONS_RPT_ID_INPUT       0x01
// clang-format on

// TODO(andresoportus): Remove bitfields.
typedef struct buttons_input_rpt {
  uint8_t rpt_id;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint8_t volume_up : 1;
  uint8_t volume_down : 1;
  uint8_t reset : 1;
  uint8_t camera_access_disabled : 1;
  uint8_t padding : 4;
#else
  uint8_t padding : 4;
  uint8_t volume_up : 1;
  uint8_t volume_down : 1;
  uint8_t reset : 1;
  uint8_t camera_access_disabled : 1;
#endif
#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint8_t mute : 1;
  uint8_t padding2 : 7;
#else
  uint8_t padding2 : 7;
  uint8_t mute : 1;
#endif
} __PACKED buttons_input_rpt_t;

size_t get_buttons_report_desc(const uint8_t** buf);
void fill_button_in_report(uint8_t id, bool value, buttons_input_rpt_t* rpt);

__END_CDECLS
