// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/descriptor.h>
#include <hid/ltr-578als.h>

#define HID_USAGE_AMBIENT_LIGHT HID_USAGE16(0x04d1)
#define HID_USAGE_HUMAN_PROXIMITY_RANGE HID_USAGE16(0x04b2)

static const uint8_t ltr_578als_report_desc[] = {
    HID_USAGE_PAGE(0x20),

    HID_COLLECTION_APPLICATION,

    HID_REPORT_ID(LTR_578ALS_RPT_ID_INPUT),

    HID_USAGE(0x41),  // Ambient light
    HID_USAGE_AMBIENT_LIGHT,
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX32(0x000fffff),
    HID_REPORT_SIZE(32),
    HID_REPORT_COUNT(1),
    HID_USAGE_SENSOR_GENERIC_UNITS_NOT_SPECIFIED,
    HID_INPUT(HID_Data_Var_Abs),

    HID_USAGE(0x12),  // Human proximity
    HID_USAGE_HUMAN_PROXIMITY_RANGE,
    HID_LOGICAL_MIN(0x0000),
    HID_LOGICAL_MAX16(0x07ff),
    HID_REPORT_SIZE(16),
    HID_REPORT_COUNT(1),
    HID_USAGE_SENSOR_GENERIC_UNITS_NOT_SPECIFIED,
    HID_INPUT(HID_Data_Var_Abs),

    HID_REPORT_ID(LTR_578ALS_RPT_ID_FEATURE),

    HID_USAGE_SENSOR_PROPERTY_REPORT_INTERVAL,
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX32(0x7fffffff),
    HID_REPORT_SIZE(32),
    HID_REPORT_COUNT(1),
    HID_FEATURE(HID_Data_Var_Abs),

    // TODO(bradenkell): Add additional configuration fields as needed.

    HID_END_COLLECTION,
};

size_t get_ltr_578als_report_desc(const uint8_t** buf) {
  *buf = ltr_578als_report_desc;
  return sizeof(ltr_578als_report_desc);
}
