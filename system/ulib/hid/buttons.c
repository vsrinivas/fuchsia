// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/buttons.h>
#include <hid/descriptor.h>

// clang-format off

static const uint8_t buttons_report_desc[] = {
    HID_USAGE_PAGE(0x0C), // Consumer
    HID_USAGE(0xE0), // Volume
    HID_COLLECTION_APPLICATION,

    HID_REPORT_ID(BUTTONS_RPT_ID_INPUT),

    HID_USAGE(0xE0), // Volume
    HID_LOGICAL_MIN(-1),
    HID_LOGICAL_MAX(1),
    HID_REPORT_SIZE(2),
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_Data_Var_Rel),
    HID_REPORT_SIZE(6), // Padding
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_Const_Arr_Abs),

    HID_END_COLLECTION,
};
// clang-format on

size_t get_buttons_report_desc(const uint8_t** buf) {
    *buf = buttons_report_desc;
    return sizeof(buttons_report_desc);
}
