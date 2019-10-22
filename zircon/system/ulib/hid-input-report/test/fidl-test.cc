// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <variant>

#include <hid-input-report/descriptors.h>
#include <hid-input-report/device.h>
#include <hid-input-report/fidl.h>
#include <hid-input-report/mouse.h>
#include <zxtest/zxtest.h>

namespace llcpp_report = ::llcpp::fuchsia::input::report;

void TestAxis(hid_input_report::Axis hid_axis, llcpp_report::Axis fidl_axis) {
  ASSERT_EQ(hid_axis.range.min, fidl_axis.range.min);
  ASSERT_EQ(hid_axis.range.max, fidl_axis.range.max);
}

TEST(FidlTest, MouseDescriptor) {
  hid_input_report::MouseDescriptor mouse_desc = {};
  mouse_desc.movement_x.enabled = true;
  mouse_desc.movement_x.unit = hid::unit::UnitType::Distance;
  mouse_desc.movement_x.range.min = -126;
  mouse_desc.movement_x.range.max = 126;

  mouse_desc.movement_y.enabled = true;
  mouse_desc.movement_y.unit = hid::unit::UnitType::Distance;
  mouse_desc.movement_y.range.min = -126;
  mouse_desc.movement_y.range.max = 126;

  mouse_desc.num_buttons = 3;
  mouse_desc.button_ids[0] = 1;
  mouse_desc.button_ids[1] = 10;
  mouse_desc.button_ids[2] = 5;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = mouse_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  llcpp_report::DeviceDescriptor fidl = fidl_desc.descriptor_builder.view();
  ASSERT_TRUE(fidl.has_mouse());
  auto& fidl_mouse = fidl.mouse();

  ASSERT_TRUE(fidl_mouse.has_movement_x());
  TestAxis(mouse_desc.movement_x, fidl_mouse.movement_x());

  ASSERT_TRUE(fidl_mouse.has_movement_y());
  TestAxis(mouse_desc.movement_y, fidl_mouse.movement_y());

  ASSERT_TRUE(fidl_mouse.has_buttons());
  ::fidl::VectorView<uint8_t>& buttons = fidl_mouse.buttons();
  ASSERT_EQ(mouse_desc.num_buttons, buttons.count());
  for (size_t i = 0; i < mouse_desc.num_buttons; i++) {
    ASSERT_EQ(mouse_desc.button_ids[i], buttons[i]);
  }
}

TEST(FidlTest, MouseReport) {
  hid_input_report::MouseReport mouse = {};
  mouse.has_movement_x = true;
  mouse.movement_x = 100;

  mouse.has_movement_y = true;
  mouse.movement_y = 200;

  mouse.num_buttons_pressed = 3;
  mouse.buttons_pressed[0] = 1;
  mouse.buttons_pressed[1] = 10;
  mouse.buttons_pressed[2] = 5;

  hid_input_report::Report report;
  report.report = mouse;

  hid_input_report::FidlReport fidl_report = {};
  ASSERT_OK(SetFidlReport(report, &fidl_report));

  llcpp_report::InputReport fidl = fidl_report.report_builder.view();
  ASSERT_TRUE(fidl.has_mouse());
  auto& fidl_mouse = fidl.mouse();

  ASSERT_TRUE(fidl_mouse.has_movement_x());
  ASSERT_EQ(mouse.movement_x, fidl_mouse.movement_x());

  ASSERT_TRUE(fidl_mouse.has_movement_y());
  ASSERT_EQ(mouse.movement_y, fidl_mouse.movement_y());

  ASSERT_TRUE(fidl_mouse.has_pressed_buttons());
  ::fidl::VectorView<uint8_t>& buttons = fidl_mouse.pressed_buttons();
  ASSERT_EQ(mouse.num_buttons_pressed, buttons.count());
  for (size_t i = 0; i < mouse.num_buttons_pressed; i++) {
    ASSERT_EQ(mouse.buttons_pressed[i], buttons[i]);
  }
}

TEST(FidlTest, SensorDescriptor) {
  hid_input_report::SensorDescriptor sensor_desc = {};
  sensor_desc.values[0].axis.enabled = true;
  sensor_desc.values[0].axis.unit = hid::unit::UnitType::LinearVelocity;
  sensor_desc.values[0].axis.range.min = 0;
  sensor_desc.values[0].axis.range.max = 1000;
  sensor_desc.values[0].type = hid::usage::Sensor::kAccelerationAxisX;

  sensor_desc.values[1].axis.enabled = true;
  sensor_desc.values[1].axis.unit = hid::unit::UnitType::Light;
  sensor_desc.values[1].axis.range.min = 0;
  sensor_desc.values[1].axis.range.max = 1000;
  sensor_desc.values[1].type = hid::usage::Sensor::kLightIlluminance;
  sensor_desc.num_values = 2;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = sensor_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  llcpp_report::DeviceDescriptor fidl = fidl_desc.descriptor_builder.view();
  ASSERT_TRUE(fidl.has_sensor());
  auto& fidl_sensor = fidl.sensor();

  ASSERT_TRUE(fidl_sensor.has_values());
  ASSERT_EQ(fidl_sensor.values().count(), 2);

  TestAxis(sensor_desc.values[0].axis, fidl_sensor.values()[0].axis);
  ASSERT_EQ(fidl_sensor.values()[0].type, llcpp_report::SensorType::ACCELEROMETER_X);

  TestAxis(sensor_desc.values[1].axis, fidl_sensor.values()[1].axis);
  ASSERT_EQ(fidl_sensor.values()[1].type, llcpp_report::SensorType::LIGHT_ILLUMINANCE);
}

TEST(FidlTest, SensorReport) {
  hid_input_report::SensorReport sensor_report = {};
  sensor_report.values[0] = 5;
  sensor_report.values[1] = -5;
  sensor_report.values[2] = 0xabcdef;
  sensor_report.num_values = 3;

  hid_input_report::Report report;
  report.report = sensor_report;

  hid_input_report::FidlReport fidl_report = {};
  ASSERT_OK(SetFidlReport(report, &fidl_report));

  llcpp_report::InputReport fidl = fidl_report.report_builder.view();
  ASSERT_TRUE(fidl.has_sensor());
  auto& fidl_sensor = fidl.sensor();

  ASSERT_TRUE(fidl_sensor.has_values());
  ASSERT_EQ(fidl_sensor.values().count(), 3);

  ASSERT_EQ(fidl_sensor.values()[0], sensor_report.values[0]);
  ASSERT_EQ(fidl_sensor.values()[1], sensor_report.values[1]);
  ASSERT_EQ(fidl_sensor.values()[2], sensor_report.values[2]);
}
