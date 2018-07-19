// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/tcs3400.h>
#include <hid/descriptor.h>

static const uint8_t tcs3400_report_desc[] = {
    HID_USAGE_PAGE(0x20), // Sensor
    HID_USAGE(0x41), // Ambient Light
    HID_COLLECTION_APPLICATION,
    HID_REPORT_ID(0x01),
    HID_USAGE16(0x04D1), // Light Illuminance
    HID_LOGICAL_MIN(0x00),
    HID_LOGICAL_MAX32(0xFFFF),
    HID_REPORT_SIZE(16),
    HID_REPORT_COUNT(1),
    HID_USAGE_SENSOR_GENERIC_UNITS_NOT_SPECIFIED, // Explicitly not Lux
    HID_INPUT(0x02), // Input Data (0=Data 1=Variable 0=Absolute 0=NoWrap 0=Linear 0=PrefState
                     //             0=NoNull 0=NonVolatile 0=Bitmap)
    HID_END_COLLECTION,
};

size_t get_tcs3400_report_desc(const uint8_t** buf) {
    *buf = tcs3400_report_desc;
    return sizeof(tcs3400_report_desc);
}
