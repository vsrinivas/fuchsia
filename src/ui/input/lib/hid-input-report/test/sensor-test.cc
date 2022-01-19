// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/sensor.h"

#include <lib/fit/defer.h>

#include <variant>

#include <hid-parser/usages.h>
#include <hid/ambient-light.h>
#include <hid/multi-sensor.h>
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

TEST(SensorTest, MultiSensor) {
  // Create the descriptor.
  hid::DeviceDescriptor* dev_desc = nullptr;
  const uint8_t* desc;
  size_t desc_size = get_multi_sensor_report_desc(&desc);
  ASSERT_NOT_NULL(desc);
  hid::ParseResult parse_res = hid::ParseReportDescriptor(desc, desc_size, &dev_desc);
  ASSERT_NOT_NULL(dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  auto free_descriptor = fit::defer([dev_desc]() { hid::FreeDeviceDescriptor(dev_desc); });
  ASSERT_EQ(5, dev_desc->rep_count);

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  fuchsia_input_report::wire::DeviceDescriptor descriptor(descriptor_allocator);
  fuchsia_input_report::wire::SensorDescriptor sensor_desc(descriptor_allocator);
  fidl::VectorView<fuchsia_input_report::wire::SensorInputDescriptor> input(descriptor_allocator,
                                                                            5);
  sensor_desc.set_input(descriptor_allocator, std::move(input));
  descriptor.set_sensor(descriptor_allocator, std::move(sensor_desc));

  std::array<hid_input_report::Sensor, 5> sensor;
  for (uint32_t i = 0; i < std::size(sensor); i++) {
    // Parse the descriptor.
    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              sensor[i].ParseReportDescriptor(dev_desc->report[i]));

    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              sensor[i].CreateDescriptor(descriptor_allocator, descriptor));
    ASSERT_TRUE(descriptor.has_sensor());
    EXPECT_TRUE(descriptor.sensor().has_input());
  }
  ASSERT_EQ(std::size(sensor), descriptor.sensor().input().count());

  // Accelerometer
  {
    ASSERT_TRUE(descriptor.sensor().input()[0].has_report_id());
    EXPECT_EQ(ACCELEROMETER_RPT_ID_B, descriptor.sensor().input()[0].report_id());
    ASSERT_TRUE(descriptor.sensor().input()[0].has_values());
    ASSERT_EQ(3, descriptor.sensor().input()[0].values().count());

    EXPECT_EQ(descriptor.sensor().input()[0].values()[0].type,
              fuchsia_input_report::wire::SensorType::kAccelerometerX);
    EXPECT_EQ(descriptor.sensor().input()[0].values()[0].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[0].values()[1].type,
              fuchsia_input_report::wire::SensorType::kAccelerometerY);
    EXPECT_EQ(descriptor.sensor().input()[0].values()[1].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[0].values()[2].type,
              fuchsia_input_report::wire::SensorType::kAccelerometerZ);
    EXPECT_EQ(descriptor.sensor().input()[0].values()[2].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    // Create the report.
    accelerometer_input_rpt_t report_data = {};
    // Values are arbitrarily chosen.
    constexpr int kAccelerometerXTestVal = 10;
    constexpr int kAccelerometerYTestVal = 20;
    constexpr int kAccelerometerZTestVal = 30;
    report_data.rpt_id = ACCELEROMETER_RPT_ID_B;
    report_data.x = kAccelerometerXTestVal;
    report_data.y = kAccelerometerYTestVal;
    report_data.z = kAccelerometerZTestVal;

    // Parse the report.
    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::InputReport input_report(report_allocator);

    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              sensor[0].ParseInputReport(reinterpret_cast<uint8_t*>(&report_data),
                                         sizeof(report_data), report_allocator, input_report));

    ASSERT_TRUE(input_report.has_sensor());
    ASSERT_EQ(3, input_report.sensor().values().count());

    // Check the report.
    // These will always match the ordering in the descriptor.
    EXPECT_EQ(kAccelerometerXTestVal, input_report.sensor().values()[0]);
    EXPECT_EQ(kAccelerometerYTestVal, input_report.sensor().values()[1]);
    EXPECT_EQ(kAccelerometerZTestVal, input_report.sensor().values()[2]);
  }

  // Gyrometer
  {
    ASSERT_TRUE(descriptor.sensor().input()[1].has_report_id());
    EXPECT_EQ(GYROMETER_RPT_ID, descriptor.sensor().input()[1].report_id());
    ASSERT_TRUE(descriptor.sensor().input()[1].has_values());
    ASSERT_EQ(3, descriptor.sensor().input()[1].values().count());

    EXPECT_EQ(descriptor.sensor().input()[1].values()[0].type,
              fuchsia_input_report::wire::SensorType::kGyroscopeX);
    EXPECT_EQ(descriptor.sensor().input()[1].values()[0].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[1].values()[1].type,
              fuchsia_input_report::wire::SensorType::kGyroscopeY);
    EXPECT_EQ(descriptor.sensor().input()[1].values()[1].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[1].values()[2].type,
              fuchsia_input_report::wire::SensorType::kGyroscopeZ);
    EXPECT_EQ(descriptor.sensor().input()[1].values()[2].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    // Create the report.
    gyrometer_input_rpt_t gyrometer_report_data = {};
    // Values are arbitrarily chosen.
    constexpr int kGyroscopeXTestVal = 12;
    constexpr int kGyroscopeYTestVal = 15;
    constexpr int kGyroscopeZTestVal = 18;
    gyrometer_report_data.rpt_id = GYROMETER_RPT_ID;
    gyrometer_report_data.x = kGyroscopeXTestVal;
    gyrometer_report_data.y = kGyroscopeYTestVal;
    gyrometer_report_data.z = kGyroscopeZTestVal;

    // Parse the report.
    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::InputReport input_report(report_allocator);

    EXPECT_EQ(
        hid_input_report::ParseResult::kOk,
        sensor[1].ParseInputReport(reinterpret_cast<uint8_t*>(&gyrometer_report_data),
                                   sizeof(gyrometer_report_data), report_allocator, input_report));

    ASSERT_TRUE(input_report.has_sensor());
    ASSERT_EQ(3, input_report.sensor().values().count());

    // Check the report.
    // These will always match the ordering in the descriptor.
    EXPECT_EQ(kGyroscopeXTestVal, input_report.sensor().values()[0]);
    EXPECT_EQ(kGyroscopeYTestVal, input_report.sensor().values()[1]);
    EXPECT_EQ(kGyroscopeZTestVal, input_report.sensor().values()[2]);
  }

  // Compass
  {
    ASSERT_TRUE(descriptor.sensor().input()[2].has_report_id());
    EXPECT_EQ(COMPASS_RPT_ID, descriptor.sensor().input()[2].report_id());
    ASSERT_TRUE(descriptor.sensor().input()[2].has_values());
    ASSERT_EQ(3, descriptor.sensor().input()[2].values().count());

    EXPECT_EQ(descriptor.sensor().input()[2].values()[0].type,
              fuchsia_input_report::wire::SensorType::kMagnetometerX);
    EXPECT_EQ(descriptor.sensor().input()[2].values()[0].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[2].values()[1].type,
              fuchsia_input_report::wire::SensorType::kMagnetometerY);
    EXPECT_EQ(descriptor.sensor().input()[2].values()[1].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[2].values()[2].type,
              fuchsia_input_report::wire::SensorType::kMagnetometerZ);
    EXPECT_EQ(descriptor.sensor().input()[2].values()[2].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    // Create the report.
    compass_input_rpt_t compass_report_data = {};
    // Values are arbitrarily chosen.
    constexpr int kCompassXTestVal = 15;
    constexpr int kCompassYTestVal = 10;
    constexpr int kCompassZTestVal = 5;
    compass_report_data.rpt_id = COMPASS_RPT_ID;
    compass_report_data.x = kCompassXTestVal;
    compass_report_data.y = kCompassYTestVal;
    compass_report_data.z = kCompassZTestVal;

    // Parse the report.
    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::InputReport input_report(report_allocator);

    EXPECT_EQ(
        hid_input_report::ParseResult::kOk,
        sensor[2].ParseInputReport(reinterpret_cast<uint8_t*>(&compass_report_data),
                                   sizeof(compass_report_data), report_allocator, input_report));

    ASSERT_TRUE(input_report.has_sensor());
    ASSERT_EQ(3, input_report.sensor().values().count());

    // Check the report.
    // These will always match the ordering in the descriptor.
    EXPECT_EQ(kCompassXTestVal, input_report.sensor().values()[0]);
    EXPECT_EQ(kCompassYTestVal, input_report.sensor().values()[1]);
    EXPECT_EQ(kCompassZTestVal, input_report.sensor().values()[2]);
  }

  // Accelerometer_B
  {
    ASSERT_TRUE(descriptor.sensor().input()[3].has_report_id());
    EXPECT_EQ(ACCELEROMETER_RPT_ID_A, descriptor.sensor().input()[3].report_id());
    ASSERT_TRUE(descriptor.sensor().input()[3].has_values());
    ASSERT_EQ(3, descriptor.sensor().input()[3].values().count());

    EXPECT_EQ(descriptor.sensor().input()[3].values()[0].type,
              fuchsia_input_report::wire::SensorType::kAccelerometerX);
    EXPECT_EQ(descriptor.sensor().input()[3].values()[0].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[3].values()[1].type,
              fuchsia_input_report::wire::SensorType::kAccelerometerY);
    EXPECT_EQ(descriptor.sensor().input()[3].values()[1].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    EXPECT_EQ(descriptor.sensor().input()[3].values()[2].type,
              fuchsia_input_report::wire::SensorType::kAccelerometerZ);
    EXPECT_EQ(descriptor.sensor().input()[3].values()[2].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    // Create the report.
    accelerometer_input_rpt_t accelerometer_report_data = {};
    // Values are arbitrarily chosen.
    constexpr int kAccelerometerXTestVal = 30;
    constexpr int kAccelerometerYTestVal = 35;
    constexpr int kAccelerometerZTestVal = 20;
    accelerometer_report_data.rpt_id = ACCELEROMETER_RPT_ID_A;
    accelerometer_report_data.x = kAccelerometerXTestVal;
    accelerometer_report_data.y = kAccelerometerYTestVal;
    accelerometer_report_data.z = kAccelerometerZTestVal;

    // Parse the report.
    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::InputReport input_report(report_allocator);

    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              sensor[3].ParseInputReport(reinterpret_cast<uint8_t*>(&accelerometer_report_data),
                                         sizeof(accelerometer_report_data), report_allocator,
                                         input_report));

    ASSERT_TRUE(input_report.has_sensor());
    ASSERT_EQ(3, input_report.sensor().values().count());

    // Check the report.
    // These will always match the ordering in the descriptor.
    EXPECT_EQ(kAccelerometerXTestVal, input_report.sensor().values()[0]);
    EXPECT_EQ(kAccelerometerYTestVal, input_report.sensor().values()[1]);
    EXPECT_EQ(kAccelerometerZTestVal, input_report.sensor().values()[2]);
  }

  // Ambient Light
  {
    ASSERT_TRUE(descriptor.sensor().input()[4].has_report_id());
    EXPECT_EQ(ILLUMINANCE_RPT_ID, descriptor.sensor().input()[4].report_id());
    ASSERT_TRUE(descriptor.sensor().input()[4].has_values());
    ASSERT_EQ(1, descriptor.sensor().input()[4].values().count());

    EXPECT_EQ(descriptor.sensor().input()[4].values()[0].type,
              fuchsia_input_report::wire::SensorType::kLightIlluminance);
    EXPECT_EQ(descriptor.sensor().input()[4].values()[0].axis.unit.type,
              fuchsia_input_report::wire::UnitType::kNone);

    // Create the report.
    illuminance_input_rpt_t illuminance_report_data = {};
    // Values are arbitrarily chosen.
    constexpr int kIlluminanceTestVal = 343;
    illuminance_report_data.rpt_id = ILLUMINANCE_RPT_ID;
    illuminance_report_data.illuminance = kIlluminanceTestVal;

    // Parse the report.
    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::InputReport input_report(report_allocator);

    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              sensor[4].ParseInputReport(reinterpret_cast<uint8_t*>(&illuminance_report_data),
                                         sizeof(illuminance_report_data), report_allocator,
                                         input_report));

    ASSERT_TRUE(input_report.has_sensor());
    ASSERT_EQ(1, input_report.sensor().values().count());

    // Check the report.
    // These will always match the ordering in the descriptor.
    EXPECT_EQ(kIlluminanceTestVal, input_report.sensor().values()[0]);
  }
}

}  // namespace hid_input_report
