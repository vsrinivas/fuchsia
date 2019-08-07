// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/bma253.h>
#include <hid/descriptor.h>

#define HID_USAGE_ACCELERATION_AXIS_X HID_USAGE16(0x0453)
#define HID_USAGE_ACCELERATION_AXIS_Y HID_USAGE16(0x0454)
#define HID_USAGE_ACCELERATION_AXIS_Z HID_USAGE16(0x0455)
#define HID_USAGE_TEMPERATURE HID_USAGE16(0x0434)

static const uint8_t bma253_report_desc[] = {
    HID_USAGE_PAGE(0x20),

    HID_COLLECTION_APPLICATION,

    HID_REPORT_ID(BMA253_RPT_ID_INPUT),

    HID_USAGE(0x73),  // Accelerometer 3D

    // The values in this report are the raw values from the sensor. See the datasheet for details
    // on how to convert them.

    HID_USAGE_ACCELERATION_AXIS_X,
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX16(0x0fff),
    HID_REPORT_SIZE(16),
    HID_REPORT_COUNT(1),
    HID_USAGE_SENSOR_GENERIC_UNITS_NOT_SPECIFIED,
    HID_INPUT(HID_Data_Var_Abs),

    HID_USAGE_ACCELERATION_AXIS_Y,
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX16(0x0fff),
    HID_REPORT_SIZE(16),
    HID_REPORT_COUNT(1),
    HID_USAGE_SENSOR_GENERIC_UNITS_NOT_SPECIFIED,
    HID_INPUT(HID_Data_Var_Abs),

    HID_USAGE_ACCELERATION_AXIS_Z,
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX16(0x0fff),
    HID_REPORT_SIZE(16),
    HID_REPORT_COUNT(1),
    HID_USAGE_SENSOR_GENERIC_UNITS_NOT_SPECIFIED,
    HID_INPUT(HID_Data_Var_Abs),

    HID_USAGE(0x33),  // Temperature
    HID_USAGE_TEMPERATURE,
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX16(0xff),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(1),
    HID_USAGE_SENSOR_GENERIC_UNITS_NOT_SPECIFIED,
    HID_INPUT(HID_Data_Var_Abs),

    HID_REPORT_ID(BMA253_RPT_ID_FEATURE),

    HID_USAGE_SENSOR_PROPERTY_REPORT_INTERVAL,
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX32(0x7fffffff),
    HID_REPORT_SIZE(32),
    HID_REPORT_COUNT(1),
    HID_FEATURE(HID_Data_Var_Abs),

    // TODO(bradenkell): Add additional configuration fields as needed.

    HID_END_COLLECTION,
};

size_t get_bma253_report_desc(const uint8_t** buf) {
  *buf = bma253_report_desc;
  return sizeof(bma253_report_desc);
}
