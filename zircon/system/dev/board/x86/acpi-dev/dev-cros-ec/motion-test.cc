// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <memory>
#include <vector>

#include <hid-parser/parser.h>
#include <zxtest/zxtest.h>

#include "dev.h"

namespace {

SensorInfo SampleSensorInfo(enum motionsensor_type type) {
  SensorInfo info = {};
  info.valid = true;
  info.type = type;
  info.loc = MOTIONSENSE_LOC_BASE;
  info.max_sampling_freq = 10;
  info.min_sampling_freq = 10;
  info.fifo_max_event_count = 1;
  info.phys_min = -3;
  info.phys_max = 3;
  return info;
}

TEST(BuildHidDescriptor, SingleDescriptorParses) {
  // Generate a HID descriptor.
  fbl::Array<uint8_t> result;
  ASSERT_OK(BuildHidDescriptor(std::vector<SensorInfo>{SampleSensorInfo(MOTIONSENSE_TYPE_ACCEL)},
                               &result));

  // Ensure it parses again.
  hid::DeviceDescriptor* parsed_hid;
  ASSERT_EQ(hid::kParseOk, hid::ParseReportDescriptor(result.data(), result.size(), &parsed_hid));
  hid::FreeDeviceDescriptor(parsed_hid);
}

TEST(BuildHidDescriptor, AllSensors) {
  // Generate a HID descriptor of all supported sensors.
  auto sensors = std::vector<SensorInfo>{
      SampleSensorInfo(MOTIONSENSE_TYPE_ACCEL),
      SampleSensorInfo(MOTIONSENSE_TYPE_GYRO),
      SampleSensorInfo(MOTIONSENSE_TYPE_MAG),
      SampleSensorInfo(MOTIONSENSE_TYPE_LIGHT),
  };
  fbl::Array<uint8_t> result;
  ASSERT_OK(BuildHidDescriptor(sensors, &result));

  // Ensure it parses again.
  hid::DeviceDescriptor* parsed_hid;
  ASSERT_EQ(hid::kParseOk, hid::ParseReportDescriptor(result.data(), result.size(), &parsed_hid));
  ASSERT_EQ(parsed_hid->rep_count, sensors.size());
  hid::FreeDeviceDescriptor(parsed_hid);
}

}  // namespace
