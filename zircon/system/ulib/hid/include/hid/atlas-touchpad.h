// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_ATLAS_TOUCHPAD_H_
#define HID_ATLAS_TOUCHPAD_H_

#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct multitouch_mouse_input_rpt {
  uint8_t report_id;

  bool button1 : 1;
  bool button2 : 1;
  uint8_t reserved1 : 6;

  uint8_t x;
  uint8_t y;

  uint8_t reserved2[5];
} __PACKED multitouch_mouse_input_rpt_t;

typedef struct contact_rpt {
  bool reserved : 1;
  bool tip_switch : 1;
  uint8_t reserved4 : 6;

  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
  uint8_t pressure;
} __PACKED contact_rpt_t;

typedef struct multitouch_touch_input_rpt {
  uint8_t report_id;

  bool button : 1;
  uint8_t reserved1 : 7;

  uint16_t reserved2;

  contact_rpt_t contact[5];
} __PACKED multitouch_touch_input_rpt_t;

typedef struct multitouch_input_mode_rpt {
  uint8_t report_id;

  uint16_t input_mode;
} __PACKED multitouch_input_mode_rpt_t;

typedef struct multitouch_selective_reporting_rpt {
  uint8_t report_id;

  bool surface_switch : 1;
  bool button_switch : 1;
  uint16_t reserved : 14;
} __PACKED multitouch_selective_reporting_rpt_t;

size_t get_atlas_touchpad_report_desc(const uint8_t** buf);

__END_CDECLS

#endif  // HID_ATLAS_TOUCHPAD_H_
