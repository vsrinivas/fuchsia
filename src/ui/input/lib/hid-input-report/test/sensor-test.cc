// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/sensor.h"

#include <variant>

#include <fbl/auto_call.h>
#include <hid-parser/usages.h>
#include <hid/ambient-light.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/device.h"
#include "src/ui/input/lib/hid-input-report/test/test.h"

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.

namespace hid_input_report {

TEST(SensorTest, AmbientLight) {
  // Create the descriptor.
  hid::DeviceDescriptor* dev_desc = nullptr;
  const uint8_t* desc;
  size_t desc_size = get_ambient_light_report_desc(&desc);
  hid::ParseResult parse_res = hid::ParseReportDescriptor(desc, desc_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  fbl::AutoCall free_descriptor([dev_desc]() { hid::FreeDeviceDescriptor(dev_desc); });

  hid_input_report::Sensor sensor;

  // Parse the descriptor.
  EXPECT_EQ(hid_input_report::ParseResult::kOk, sensor.ParseReportDescriptor(dev_desc->report[1]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            sensor.CreateDescriptor(&descriptor_allocator, &descriptor_builder));
  fuchsia_input_report::DeviceDescriptor descriptor = descriptor_builder.build();
  EXPECT_TRUE(descriptor.has_sensor());
  EXPECT_TRUE(descriptor.sensor().has_input());

  // Check the descriptor.
  ASSERT_EQ(4, descriptor.sensor().input().values().count());

  ASSERT_EQ(descriptor.sensor().input().values()[0].type,
            ::llcpp::fuchsia::input::report::SensorType::LIGHT_ILLUMINANCE);
  ASSERT_EQ(descriptor.sensor().input().values()[0].axis.unit.type,
            ::llcpp::fuchsia::input::report::UnitType::NONE);

  ASSERT_EQ(descriptor.sensor().input().values()[1].type,
            ::llcpp::fuchsia::input::report::SensorType::LIGHT_RED);
  ASSERT_EQ(descriptor.sensor().input().values()[1].axis.unit.type,
            ::llcpp::fuchsia::input::report::UnitType::NONE);

  ASSERT_EQ(descriptor.sensor().input().values()[2].type,
            ::llcpp::fuchsia::input::report::SensorType::LIGHT_BLUE);
  ASSERT_EQ(descriptor.sensor().input().values()[2].axis.unit.type,
            ::llcpp::fuchsia::input::report::UnitType::NONE);

  ASSERT_EQ(descriptor.sensor().input().values()[3].type,
            ::llcpp::fuchsia::input::report::SensorType::LIGHT_GREEN);
  ASSERT_EQ(descriptor.sensor().input().values()[3].axis.unit.type,
            ::llcpp::fuchsia::input::report::UnitType::NONE);

  // Create the report.
  ambient_light_input_rpt_t report_data = {};
  // Values are arbitrarily chosen.
  constexpr int kIlluminanceTestVal = 10;
  constexpr int kRedTestVal = 101;
  constexpr int kBlueTestVal = 5;
  constexpr int kGreenTestVal = 3;
  report_data.rpt_id = AMBIENT_LIGHT_RPT_ID_INPUT;
  report_data.illuminance = kIlluminanceTestVal;
  report_data.red = kRedTestVal;
  report_data.blue = kBlueTestVal;
  report_data.green = kGreenTestVal;

  // Parse the report.
  hid_input_report::TestReportAllocator report_allocator;
  auto report_builder = fuchsia_input_report::InputReport::Builder(
      report_allocator.make<fuchsia_input_report::InputReport::Frame>());

  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            sensor.ParseInputReport(reinterpret_cast<uint8_t*>(&report_data), sizeof(report_data),
                                    &report_allocator, &report_builder));

  fuchsia_input_report::InputReport input_report = report_builder.build();
  ASSERT_TRUE(input_report.has_sensor());
  EXPECT_EQ(4, input_report.sensor().values().count());

  // Check the report.
  // These will always match the ordering in the descriptor.
  EXPECT_EQ(kIlluminanceTestVal, input_report.sensor().values()[0]);
  EXPECT_EQ(kRedTestVal, input_report.sensor().values()[1]);
  EXPECT_EQ(kBlueTestVal, input_report.sensor().values()[2]);
  EXPECT_EQ(kGreenTestVal, input_report.sensor().values()[3]);
}

TEST(SensorTest, DeviceType) {
  Sensor device;
  ASSERT_EQ(DeviceType::kSensor, device.GetDeviceType());
}

}  // namespace hid_input_report
