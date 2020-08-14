// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/metadata/buttons.h>
#include <hid/buttons.h>
#include <hid/descriptor.h>

// clang-format off
static const uint8_t buttons_report_desc[] = {
    HID_USAGE_PAGE(0x0C), // Consumer
    HID_USAGE(0x01), // Consumer Control
    HID_COLLECTION_APPLICATION,

    HID_REPORT_ID(BUTTONS_RPT_ID_INPUT),

    HID_USAGE_PAGE(0x0C), // Consumer
    HID_USAGE(0xE9), // Volume Up
    HID_USAGE(0xEA), // Volume Down
    HID_USAGE(0x31), // Reset
    HID_USAGE(0x77), // Camera access disabled
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(4),
    HID_INPUT(HID_Data_Var_Abs),
    HID_REPORT_SIZE(4), // Padding
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_Const_Arr_Abs),

    HID_USAGE_PAGE(0x0B), // Telephony
    HID_USAGE(0x2F), // Mute microphone
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_Data_Var_Abs),
    HID_REPORT_SIZE(7), // Padding
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_Const_Arr_Abs),

    HID_END_COLLECTION,
};
// clang-format on

size_t get_buttons_report_desc(const uint8_t** buf) {
  *buf = buttons_report_desc;
  return sizeof(buttons_report_desc);
}

void fill_button_in_report(uint8_t id, bool value, buttons_input_rpt_t* rpt) {
  switch (id) {
    case BUTTONS_ID_VOLUME_UP:
      if (value) {
        rpt->volume_up = 1;
      }
      break;
    case BUTTONS_ID_VOLUME_DOWN:
      if (value) {
        rpt->volume_down = 1;
      }
      break;
    case BUTTONS_ID_FDR:
      if (value) {
        rpt->reset = 1;
      }
      break;
    case BUTTONS_ID_MIC_MUTE:
      rpt->mute = value;
      break;
    case BUTTONS_ID_CAM_MUTE:
      rpt->camera_access_disabled = value;
      break;
  }
}
