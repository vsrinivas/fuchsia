// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TESTS_SENSOR_TEST_DATA_H_
#define GARNET_BIN_UI_INPUT_READER_TESTS_SENSOR_TEST_DATA_H_

#include <stdint.h>

// A made-up light sensor report descriptor to test the scalar values.
static const uint8_t lightmeter_report_desc[] = {
    0x05, 0x20,  // HID Usage Page (Sensors)
    0x09, 0x01,  // HID Usage (Sensor)
    0xa1, 0x01,  // Collection (Application)

    0x85, 0x04,        // Report ID (4)
    0x05, 0x20,        // HID Usage Page (Sensors)
    0x09, 0x41,        // HID Usage (Ambient Light)
    0xa1, 0x00,        // Collection (Physical)
    0x05, 0x20,        // HID Usage Page (Sensors)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xff, 0x7f,  // Logical Maximum (32767)
    0x75, 0x10,        // Report Size (16)
    0x95, 0x01,        // Report Count (1)
    0x0a, 0xd1, 0x04,  // Usage (Illuminance)
    0x81, 0x03,        // Const Var Abs
    0xc0,              // End Collection
    0xc0,              // End Collection
};

// A made-up accelerometer report descriptor to test the axis values.
static const uint8_t accelerometer_report_desc[] = {
    0x05, 0x20,  // HID Usage Page (Sensors)
    0x09, 0x01,  // HID Usage (Sensor)
    0xa1, 0x01,  // Collection (Application)

    // Base Accelerometer
    0x85, 0x01,        // Report ID (1)
    0x05, 0x20,        // HID Usage Page (Sensors)
    0x09, 0x73,        // HID Usage (Accelerometer 3D)
    0xa1, 0x00,        // Collection (Physical)
    0x05, 0x20,        // HID Usage Page (Sensors)
    0x16, 0x00, 0x80,  // Logical Minimum (-32768)
    0x26, 0xff, 0x7f,  // Logical Maximum (32767)
    0x75, 0x10,        // Report Size (16)
    0x95, 0x01,        // Report Count (1)
    0x0a, 0x53, 0x04,  // Usage (Acceleration Axis X)
    0x81, 0x03,        // Const Var Abs
    0x0a, 0x54, 0x04,  // Usage (Acceleration Axis Y)
    0x81, 0x03,        // Const Var Abs
    0x0a, 0x55, 0x04,  // Usage (Acceleration Axis Z)
    0x81, 0x03,        // Const Var Abs
    0xc0,              // End Collection
    0xc0,              // End Collection
};

#endif  // GARNET_BIN_UI_INPUT_READER_TESTS_SENSOR_TEST_DATA_H_
