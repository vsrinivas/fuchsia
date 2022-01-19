// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/sensor.h"

#include <lib/fit/defer.h>

#include <variant>

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
  auto free_descriptor = fit::defer([dev_desc]() { hid::FreeDeviceDescriptor(dev_desc); });

  hid_input_report::Sensor sensor;

  // Parse the descriptor.
  EXPECT_EQ(hid_input_report::ParseResult::kOk, sensor.ParseReportDescriptor(dev_desc->report[1]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  fuchsia_input_report::wire::DeviceDescriptor descriptor(descriptor_allocator);
  fuchsia_input_report::wire::SensorDescriptor sensor_desc(descriptor_allocator);
  fidl::VectorView<fuchsia_input_report::wire::SensorInputDescriptor> input(descriptor_allocator,
                                                                            1);
  sensor_desc.set_input(descriptor_allocator, std::move(input));
  descriptor.set_sensor(descriptor_allocator, std::move(sensor_desc));
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            sensor.CreateDescriptor(descriptor_allocator, descriptor));
  EXPECT_TRUE(descriptor.has_sensor());
  EXPECT_TRUE(descriptor.sensor().has_input());

  // Check the descriptor.
  ASSERT_EQ(1, descriptor.sensor().input().count());
  ASSERT_EQ(4, descriptor.sensor().input()[0].values().count());

  ASSERT_EQ(descriptor.sensor().input()[0].values()[0].type,
            fuchsia_input_report::wire::SensorType::kLightIlluminance);
  ASSERT_EQ(descriptor.sensor().input()[0].values()[0].axis.unit.type,
            fuchsia_input_report::wire::UnitType::kNone);

  ASSERT_EQ(descriptor.sensor().input()[0].values()[1].type,
            fuchsia_input_report::wire::SensorType::kLightRed);
  ASSERT_EQ(descriptor.sensor().input()[0].values()[1].axis.unit.type,
            fuchsia_input_report::wire::UnitType::kNone);

  ASSERT_EQ(descriptor.sensor().input()[0].values()[2].type,
            fuchsia_input_report::wire::SensorType::kLightBlue);
  ASSERT_EQ(descriptor.sensor().input()[0].values()[2].axis.unit.type,
            fuchsia_input_report::wire::UnitType::kNone);

  ASSERT_EQ(descriptor.sensor().input()[0].values()[3].type,
            fuchsia_input_report::wire::SensorType::kLightGreen);
  ASSERT_EQ(descriptor.sensor().input()[0].values()[3].axis.unit.type,
            fuchsia_input_report::wire::UnitType::kNone);

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
  fuchsia_input_report::wire::InputReport input_report(report_allocator);

  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            sensor.ParseInputReport(reinterpret_cast<uint8_t*>(&report_data), sizeof(report_data),
                                    report_allocator, input_report));

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
