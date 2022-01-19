// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/descriptor.h>
#include <hid/multi-sensor.h>

// clang-format off

#define HID_USAGE_ACCELERATION_X      HID_USAGE16(0x0453)
#define HID_USAGE_ACCELERATION_Y      HID_USAGE16(0x0454)
#define HID_USAGE_ACCELERATION_Z      HID_USAGE16(0x0455)
#define HID_USAGE_ANGULAR_VELOCITY_X  HID_USAGE16(0x0457)
#define HID_USAGE_ANGULAR_VELOCITY_Y  HID_USAGE16(0x0458)
#define HID_USAGE_ANGULAR_VELOCITY_Z  HID_USAGE16(0x0459)
#define HID_USAGE_MAGNETIC_FLUX_X     HID_USAGE16(0x0485)
#define HID_USAGE_MAGNETIC_FLUX_Y     HID_USAGE16(0x0486)
#define HID_USAGE_MAGNETIC_FLUX_Z     HID_USAGE16(0x0487)
#define HID_USAGE_AMBIENT_LIGHT       HID_USAGE16(0x04D1)

static const uint8_t multi_sensor_report_desc[] = {
    HID_USAGE_PAGE(0x20), // Sensor
    HID_USAGE(0x01),      // Sensor
    HID_COLLECTION_APPLICATION,
        HID_REPORT_ID(ACCELEROMETER_RPT_ID_B),
        // Removing physical range for ease of testing
        // HID_PHYSICAL_MIN32(0xfffffffe),
        // HID_PHYSICAL_MAX32(0x2),
        HID_USAGE_PAGE(0x20),  // Sensor
        HID_USAGE(0x73),       // Motion: Accelerometer 3D
        HID_COLLECTION_PHYSICAL,
            HID_USAGE_PAGE(0x20),  // Sensor
            HID_LOGICAL_MIN16(0x8000),
            HID_LOGICAL_MAX16(0x7fff),
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(1),
            HID_USAGE_ACCELERATION_X,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_ACCELERATION_Y,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_ACCELERATION_Z,
            HID_INPUT(HID_Const_Var_Abs),
        HID_END_COLLECTION,

        HID_REPORT_ID(GYROMETER_RPT_ID),
        // Removing physical range for ease of testing
        // HID_PHYSICAL_MIN32(0xfffffc18),
        // HID_PHYSICAL_MAX32(0x3e8),
        HID_USAGE_PAGE(0x20),  // Sensor
        HID_USAGE(0x76),       // Motion: Gyrometer 3D
        HID_COLLECTION_PHYSICAL,
            HID_USAGE_PAGE(0x20),  // Sensor
            HID_LOGICAL_MIN16(0x8000),
            HID_LOGICAL_MAX16(0x7fff),
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(1),
            HID_USAGE_ANGULAR_VELOCITY_X,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_ANGULAR_VELOCITY_Y,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_ANGULAR_VELOCITY_Z,
            HID_INPUT(HID_Const_Var_Abs),
        HID_END_COLLECTION,

        HID_REPORT_ID(COMPASS_RPT_ID),
        // Removing physical range for ease of testing
        // HID_PHYSICAL_MIN32(0xffec7800),
        // HID_PHYSICAL_MAX32(0x138800),
        HID_USAGE_PAGE(0x20),  // Sensor
        HID_USAGE(0x83),       // Orientation: Compass 3D
        HID_COLLECTION_PHYSICAL,
            HID_USAGE_PAGE(0x20),  // Sensor
            HID_LOGICAL_MIN16(0x8000),
            HID_LOGICAL_MAX16(0x7fff),
            HID_UNIT_EXPONENT(0xd),
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(1),
            HID_USAGE_MAGNETIC_FLUX_X,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_MAGNETIC_FLUX_Y,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_MAGNETIC_FLUX_Z,
            HID_INPUT(HID_Const_Var_Abs),
        HID_END_COLLECTION,
    HID_END_COLLECTION,

    HID_USAGE_PAGE(0x20), // Sensor
    HID_USAGE(0x01),      // Sensor
    HID_COLLECTION_APPLICATION,
        HID_REPORT_ID(ACCELEROMETER_RPT_ID_A),
        // Removing physical range for ease of testing
        // HID_PHYSICAL_MIN32(0xfffffffe),
        // HID_PHYSICAL_MAX32(0x2),
        HID_USAGE_PAGE(0x20),  // Sensor
        HID_USAGE(0x73),       // Motion: Accelerometer 3D
        HID_COLLECTION_PHYSICAL,
            HID_USAGE_PAGE(0x20),  // Sensor
            HID_LOGICAL_MIN16(0x8000),
            HID_LOGICAL_MAX16(0x7fff),
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(1),
            HID_USAGE_ACCELERATION_X,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_ACCELERATION_Y,
            HID_INPUT(HID_Const_Var_Abs),
            HID_USAGE_ACCELERATION_Z,
            HID_INPUT(HID_Const_Var_Abs),
        HID_END_COLLECTION,

        HID_REPORT_ID(ILLUMINANCE_RPT_ID),
        // Removing physical range for ease of testing
        // HID_PHYSICAL_MIN32(0),
        // HID_PHYSICAL_MAX32(0x1770),
        HID_USAGE_PAGE(0x20),  // Sensor
        HID_USAGE(0x41),       // Light: Ambient Light
        HID_COLLECTION_PHYSICAL,
            HID_USAGE_PAGE(0x20),  // Sensor
            HID_LOGICAL_MIN(0),
            HID_LOGICAL_MAX16(0x7fff),
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(1),
            HID_USAGE_AMBIENT_LIGHT,
            HID_INPUT(HID_Const_Var_Abs),
        HID_END_COLLECTION,
    HID_END_COLLECTION,
};
// clang-format on

size_t get_multi_sensor_report_desc(const uint8_t** buf) {
  *buf = multi_sensor_report_desc;
  return sizeof(multi_sensor_report_desc);
}
