// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/sensor.h"

#include <gtest/gtest.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>

#include "src/ui/lib/input_reader/tests/sensor_test_data.h"

namespace input {

namespace test {

TEST(SensorTest, LightMeter) {
  ui_input::Sensor sensor;
  hid::DeviceDescriptor *hid_desc = nullptr;

  auto parse_res =
      hid::ParseReportDescriptor(lightmeter_report_desc, sizeof(lightmeter_report_desc), &hid_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  ASSERT_NE(0UL, hid_desc->rep_count);
  ASSERT_NE(0UL, hid_desc->report[0].input_count);

  ui_input::Device::Descriptor descriptor = {};
  auto success = sensor.ParseReportDescriptor(hid_desc->report[0], &descriptor);
  ASSERT_EQ(true, success);

  ASSERT_EQ(ui_input::Protocol::Sensor, descriptor.protocol);
  ASSERT_EQ(true, descriptor.has_sensor);
  ASSERT_NE(nullptr, descriptor.sensor_descriptor);
  ASSERT_EQ(fuchsia::ui::input::SensorType::LIGHTMETER, descriptor.sensor_descriptor->type);

  uint8_t report_data[] = {
      0x04,        // Report ID
      0x12, 0x24,  // Illuminance
  };

  auto sensor_report = fuchsia::ui::input::InputReport::New();
  sensor_report->sensor = fuchsia::ui::input::SensorReport::New();

  success = sensor.ParseReport(report_data, sizeof(report_data), sensor_report.get());
  EXPECT_EQ(true, success);

  EXPECT_EQ(0x2412, sensor_report->sensor->scalar());
}

TEST(SensorTest, Accelerometer) {
  ui_input::Sensor sensor;
  hid::DeviceDescriptor *hid_desc = nullptr;

  auto parse_res = hid::ParseReportDescriptor(accelerometer_report_desc,
                                              sizeof(accelerometer_report_desc), &hid_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  ASSERT_NE(0UL, hid_desc->rep_count);
  ASSERT_NE(0UL, hid_desc->report[0].input_count);

  ui_input::Device::Descriptor descriptor = {};
  auto success = sensor.ParseReportDescriptor(hid_desc->report[0], &descriptor);
  ASSERT_EQ(true, success);

  ASSERT_EQ(ui_input::Protocol::Sensor, descriptor.protocol);
  ASSERT_EQ(true, descriptor.has_sensor);
  ASSERT_NE(nullptr, descriptor.sensor_descriptor);
  ASSERT_EQ(fuchsia::ui::input::SensorType::ACCELEROMETER, descriptor.sensor_descriptor->type);

  uint8_t report_data[] = {
      0x01,        // Report ID
      0xFF, 0x00,  // X - Axis
      0xFF, 0xFF,  // Y - Axis
      0x00, 0x80,  // Z - Axis
  };

  auto sensor_report = fuchsia::ui::input::InputReport::New();
  sensor_report->sensor = fuchsia::ui::input::SensorReport::New();

  success = sensor.ParseReport(report_data, sizeof(report_data), sensor_report.get());
  EXPECT_EQ(true, success);

  EXPECT_EQ(0xFF, sensor_report->sensor->vector()[0]);
  EXPECT_EQ(-1, sensor_report->sensor->vector()[1]);
  EXPECT_EQ(-32768, sensor_report->sensor->vector()[2]);
}

}  // namespace test
}  // namespace input
