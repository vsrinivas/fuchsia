// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/tcs3400.h>

static const uint8_t tcs3400_report_desc[] = {
    0x05, 0x20,                   // Usage Page (Sensor)
    0x09, 0x41,                   // Usage (Ambient Light)
    0xA1, 0x01,                   // Collection (Application)
    0x85, 0x01,                   //   Report ID (1)
    0x0A, 0xD1, 0x04,             //   Usage (Light Illuminance)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, //   Logical Maximum (65534)
    0x75, 0x10,                   //   Report Size (16 bits per field)
    0x95, 0x01,                   //   Report Count (1: Illuminance)
    0x65, 0x00,                   //   Units (Not Specified, explicitly not Lux)
    0x81, 0x02,                   //   Input Data (0=Data 1=Variable 0=Absolute 0=NoWrap 0=Linear
                                  //               0=PrefState 0=NoNull 0=NonVolatile 0=Bitmap)
    0xC0,                         // End Collection
};

size_t get_tcs3400_report_desc(const uint8_t** buf) {
    *buf = tcs3400_report_desc;
    return sizeof(tcs3400_report_desc);
}
