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

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

void TestAxis(fuchsia_input_report::Axis a, fuchsia_input_report::Axis b) {
  ASSERT_EQ(a.range.min, b.range.min);
  ASSERT_EQ(a.range.max, b.range.max);
  ASSERT_EQ(a.unit, b.unit);
}

TEST(FidlTest, MouseDescriptor) {
  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::DISTANCE;
  axis.range.min = -126;
  axis.range.max = 126;

  hid_input_report::MouseDescriptor mouse_desc = {};
  mouse_desc.movement_x = axis;
  mouse_desc.movement_y = axis;
  mouse_desc.num_buttons = 3;
  mouse_desc.buttons[0] = 1;
  mouse_desc.buttons[1] = 10;
  mouse_desc.buttons[2] = 5;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = mouse_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.view();
  ASSERT_TRUE(fidl.has_mouse());
  auto& fidl_mouse = fidl.mouse();

  ASSERT_TRUE(fidl_mouse.has_movement_x());
  TestAxis(*mouse_desc.movement_x, fidl_mouse.movement_x());

  ASSERT_TRUE(fidl_mouse.has_movement_y());
  TestAxis(*mouse_desc.movement_y, fidl_mouse.movement_y());

  ASSERT_TRUE(fidl_mouse.has_buttons());
  ::fidl::VectorView<uint8_t>& buttons = fidl_mouse.buttons();
  ASSERT_EQ(mouse_desc.num_buttons, buttons.count());
  for (size_t i = 0; i < mouse_desc.num_buttons; i++) {
    ASSERT_EQ(mouse_desc.buttons[i], buttons[i]);
  }
}

TEST(FidlTest, MouseReport) {
  hid_input_report::MouseReport mouse = {};
  mouse.movement_x = 100;

  mouse.movement_y = 200;

  mouse.num_buttons_pressed = 3;
  mouse.buttons_pressed[0] = 1;
  mouse.buttons_pressed[1] = 10;
  mouse.buttons_pressed[2] = 5;

  hid_input_report::Report report;
  report.report = mouse;

  hid_input_report::FidlReport fidl_report = {};
  ASSERT_OK(SetFidlReport(report, &fidl_report));

  fuchsia_input_report::InputReport fidl = fidl_report.builder.view();
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
  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::LINEAR_VELOCITY;
  axis.range.min = -126;
  axis.range.max = 126;

  hid_input_report::SensorDescriptor sensor_desc = {};
  sensor_desc.values[0].axis = axis;
  sensor_desc.values[0].type = fuchsia_input_report::SensorType::ACCELEROMETER_X;

  axis.unit = fuchsia_input_report::Unit::LUX;
  sensor_desc.values[1].axis = axis;
  sensor_desc.values[1].type = fuchsia_input_report::SensorType::LIGHT_ILLUMINANCE;

  sensor_desc.num_values = 2;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = sensor_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.view();
  ASSERT_TRUE(fidl.has_sensor());
  auto& fidl_sensor = fidl.sensor();

  ASSERT_TRUE(fidl_sensor.has_values());
  ASSERT_EQ(fidl_sensor.values().count(), 2);

  TestAxis(sensor_desc.values[0].axis, fidl_sensor.values()[0].axis);
  ASSERT_EQ(fidl_sensor.values()[0].type, fuchsia_input_report::SensorType::ACCELEROMETER_X);

  TestAxis(sensor_desc.values[1].axis, fidl_sensor.values()[1].axis);
  ASSERT_EQ(fidl_sensor.values()[1].type, fuchsia_input_report::SensorType::LIGHT_ILLUMINANCE);
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

  fuchsia_input_report::InputReport fidl = fidl_report.builder.view();
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
  touch_desc.touch_type = fuchsia_input_report::TouchType::TOUCHSCREEN;

  touch_desc.max_contacts = 100;

  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::DISTANCE;
  axis.range.min = 0;
  axis.range.max = 0xabcdef;

  touch_desc.contacts[0].position_x = axis;
  touch_desc.contacts[0].position_y = axis;

  axis.unit = fuchsia_input_report::Unit::PRESSURE;
  axis.range.min = 0;
  axis.range.max = 100;
  touch_desc.contacts[0].pressure = axis;

  touch_desc.num_contacts = 1;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = touch_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.view();
  ASSERT_TRUE(fidl.has_touch());
  auto& fidl_touch = fidl.touch();

  ASSERT_TRUE(fidl_touch.has_max_contacts());
  ASSERT_EQ(touch_desc.max_contacts, fidl_touch.max_contacts());

  ASSERT_EQ(touch_desc.touch_type, fidl_touch.touch_type());

  ASSERT_EQ(1, fidl_touch.contacts().count());

  TestAxis(*touch_desc.contacts[0].position_x, fidl_touch.contacts()[0].position_x());
  TestAxis(*touch_desc.contacts[0].position_y, fidl_touch.contacts()[0].position_y());
  TestAxis(*touch_desc.contacts[0].pressure, fidl_touch.contacts()[0].pressure());
}

TEST(FidlTest, TouchReport) {
  hid_input_report::TouchReport touch_report = {};

  touch_report.num_contacts = 1;

  touch_report.contacts[0].position_x = 123;
  touch_report.contacts[0].position_y = 234;
  touch_report.contacts[0].pressure = 345;
  touch_report.contacts[0].contact_width = 678;
  touch_report.contacts[0].contact_height = 789;

  hid_input_report::Report report;
  report.report = touch_report;

  hid_input_report::FidlReport fidl_report = {};
  ASSERT_OK(SetFidlReport(report, &fidl_report));

  fuchsia_input_report::InputReport fidl = fidl_report.builder.view();
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
  keyboard_descriptor.keys[0] = llcpp::fuchsia::ui::input2::Key::A;
  keyboard_descriptor.keys[1] = llcpp::fuchsia::ui::input2::Key::END;
  keyboard_descriptor.keys[2] = llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT;

  hid_input_report::ReportDescriptor descriptor;
  descriptor.descriptor = keyboard_descriptor;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(descriptor, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.view();
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
  keyboard_report.pressed_keys[0] = llcpp::fuchsia::ui::input2::Key::A;
  keyboard_report.pressed_keys[1] = llcpp::fuchsia::ui::input2::Key::END;
  keyboard_report.pressed_keys[2] = llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT;

  hid_input_report::Report report;
  report.report = keyboard_report;

  hid_input_report::FidlReport fidl_report = {};
  ASSERT_OK(SetFidlReport(report, &fidl_report));

  fuchsia_input_report::InputReport fidl = fidl_report.builder.view();
  ASSERT_TRUE(fidl.has_keyboard());
  auto& fidl_keyboard = fidl.keyboard();

  ASSERT_EQ(3, fidl_keyboard.pressed_keys().count());
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::A, fidl_keyboard.pressed_keys()[0]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::END, fidl_keyboard.pressed_keys()[1]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT, fidl_keyboard.pressed_keys()[2]);
}
