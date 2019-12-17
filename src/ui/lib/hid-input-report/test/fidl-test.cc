// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/fidl.h"

#include <variant>

#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "src/ui/lib/hid-input-report/descriptors.h"
#include "src/ui/lib/hid-input-report/device.h"
#include "src/ui/lib/hid-input-report/mouse.h"

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

  llcpp_report::DeviceDescriptor fidl = fidl_desc.builder.view();
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

  llcpp_report::InputReport fidl = fidl_report.builder.view();
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

  llcpp_report::DeviceDescriptor fidl = fidl_desc.builder.view();
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

  llcpp_report::InputReport fidl = fidl_report.builder.view();
  ASSERT_TRUE(fidl.has_sensor());
  auto& fidl_sensor = fidl.sensor();

  ASSERT_TRUE(fidl_sensor.has_values());
  ASSERT_EQ(fidl_sensor.values().count(), 3);

  ASSERT_EQ(fidl_sensor.values()[0], sensor_report.values[0]);
  ASSERT_EQ(fidl_sensor.values()[1], sensor_report.values[1]);
  ASSERT_EQ(fidl_sensor.values()[2], sensor_report.values[2]);
}

TEST(FidlTest, TouchDescriptor) {
  hid_input_report::TouchDescriptor touch_desc = {};
  touch_desc.touch_type = llcpp_report::TouchType::TOUCHSCREEN;

  touch_desc.max_contacts = 100;

  touch_desc.contacts[0].position_x.enabled = true;
  touch_desc.contacts[0].position_x.range.min = 0;
  touch_desc.contacts[0].position_x.range.max = 0xabcdef;

  touch_desc.contacts[0].position_y.enabled = true;
  touch_desc.contacts[0].position_y.range.min = 0;
  touch_desc.contacts[0].position_y.range.max = 0xabcdef;

  touch_desc.contacts[0].pressure.enabled = true;
  touch_desc.contacts[0].pressure.range.min = 0;
  touch_desc.contacts[0].pressure.range.max = 100;

  touch_desc.num_contacts = 1;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = touch_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  llcpp_report::DeviceDescriptor fidl = fidl_desc.builder.view();
  ASSERT_TRUE(fidl.has_touch());
  auto& fidl_touch = fidl.touch();

  ASSERT_TRUE(fidl_touch.has_max_contacts());
  ASSERT_EQ(touch_desc.max_contacts, fidl_touch.max_contacts());

  ASSERT_EQ(touch_desc.touch_type, fidl_touch.touch_type());

  ASSERT_EQ(1, fidl_touch.contacts().count());

  TestAxis(touch_desc.contacts[0].position_x, fidl_touch.contacts()[0].position_x());
  TestAxis(touch_desc.contacts[0].position_y, fidl_touch.contacts()[0].position_y());
  TestAxis(touch_desc.contacts[0].pressure, fidl_touch.contacts()[0].pressure());
}

TEST(FidlTest, TouchReport) {
  hid_input_report::TouchReport touch_report = {};

  touch_report.num_contacts = 1;

  touch_report.contacts[0].has_position_x = true;
  touch_report.contacts[0].position_x = 123;

  touch_report.contacts[0].has_position_y = true;
  touch_report.contacts[0].position_y = 234;

  touch_report.contacts[0].has_pressure = true;
  touch_report.contacts[0].pressure = 345;

  touch_report.contacts[0].has_contact_width = true;
  touch_report.contacts[0].contact_width = 678;

  touch_report.contacts[0].has_contact_height = true;
  touch_report.contacts[0].contact_height = 789;

  hid_input_report::Report report;
  report.report = touch_report;

  hid_input_report::FidlReport fidl_report = {};
  ASSERT_OK(SetFidlReport(report, &fidl_report));

  llcpp_report::InputReport fidl = fidl_report.builder.view();
  ASSERT_TRUE(fidl.has_touch());
  auto& fidl_touch = fidl.touch();

  ASSERT_EQ(1, fidl_touch.contacts().count());

  EXPECT_EQ(touch_report.contacts[0].position_x, fidl_touch.contacts()[0].position_x());
  EXPECT_EQ(touch_report.contacts[0].position_y, fidl_touch.contacts()[0].position_y());
  EXPECT_EQ(touch_report.contacts[0].pressure, fidl_touch.contacts()[0].pressure());
  EXPECT_EQ(touch_report.contacts[0].contact_width, fidl_touch.contacts()[0].contact_width());
  EXPECT_EQ(touch_report.contacts[0].contact_height, fidl_touch.contacts()[0].contact_height());
}

TEST(FidlTest, KeyboardDescriptor) {
  hid_input_report::KeyboardDescriptor keyboard_descriptor = {};
  keyboard_descriptor.num_keys = 3;
  keyboard_descriptor.keys[0] = HID_USAGE_KEY_A;
  keyboard_descriptor.keys[1] = HID_USAGE_KEY_END;
  keyboard_descriptor.keys[2] = HID_USAGE_KEY_LEFT_SHIFT;

  hid_input_report::ReportDescriptor descriptor;
  descriptor.descriptor = keyboard_descriptor;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(descriptor, &fidl_desc));

  llcpp_report::DeviceDescriptor fidl = fidl_desc.builder.view();
  ASSERT_TRUE(fidl.has_keyboard());
  auto& fidl_keyboard = fidl.keyboard();

  ASSERT_EQ(3, fidl_keyboard.keys().count());
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::A, fidl_keyboard.keys()[0]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::END, fidl_keyboard.keys()[1]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT, fidl_keyboard.keys()[2]);
}

TEST(FidlTest, KeyboardReport) {
  hid_input_report::KeyboardReport keyboard_report = {};
  keyboard_report.num_pressed_keys = 3;
  keyboard_report.pressed_keys[0] = HID_USAGE_KEY_A;
  keyboard_report.pressed_keys[1] = HID_USAGE_KEY_END;
  keyboard_report.pressed_keys[2] = HID_USAGE_KEY_LEFT_SHIFT;

  hid_input_report::Report report;
  report.report = keyboard_report;

  hid_input_report::FidlReport fidl_report = {};
  ASSERT_OK(SetFidlReport(report, &fidl_report));

  llcpp_report::InputReport fidl = fidl_report.builder.view();
  ASSERT_TRUE(fidl.has_keyboard());
  auto& fidl_keyboard = fidl.keyboard();

  ASSERT_EQ(3, fidl_keyboard.pressed_keys().count());
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::A, fidl_keyboard.pressed_keys()[0]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::END, fidl_keyboard.pressed_keys()[1]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT, fidl_keyboard.pressed_keys()[2]);
}
