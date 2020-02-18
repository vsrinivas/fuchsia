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

TEST(FidlTest, MouseInputDescriptor) {
  // Build Mouse Input Descriptor.
  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::DISTANCE;
  axis.range.min = -126;
  axis.range.max = 126;

  hid_input_report::MouseDescriptor mouse_desc = {};
  mouse_desc.input = hid_input_report::MouseInputDescriptor();
  mouse_desc.input->movement_x = axis;
  mouse_desc.input->movement_y = axis;
  mouse_desc.input->num_buttons = 3;
  mouse_desc.input->buttons[0] = 1;
  mouse_desc.input->buttons[1] = 10;
  mouse_desc.input->buttons[2] = 5;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = mouse_desc;

  // Convert to FIDL and back.
  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.build();
  ASSERT_TRUE(fidl.has_mouse());
  ASSERT_TRUE(fidl.mouse().has_input());

  hid_input_report::MouseDescriptor new_desc = hid_input_report::ToMouseDescriptor(fidl.mouse());

  // Check the fields.
  TestAxis(*mouse_desc.input->movement_x, *new_desc.input->movement_x);
  TestAxis(*mouse_desc.input->movement_y, *new_desc.input->movement_y);

  ASSERT_EQ(mouse_desc.input->num_buttons, new_desc.input->num_buttons);
  for (size_t i = 0; i < mouse_desc.input->num_buttons; i++) {
    ASSERT_EQ(mouse_desc.input->buttons[i], mouse_desc.input->buttons[i]);
  }
}

TEST(FidlTest, MouseInputReport) {
  // Build Mouse Input report.
  hid_input_report::MouseInputReport mouse = {};
  mouse.movement_x = 100;
  mouse.movement_y = 200;
  mouse.num_buttons_pressed = 3;
  mouse.buttons_pressed[0] = 1;
  mouse.buttons_pressed[1] = 10;
  mouse.buttons_pressed[2] = 5;

  hid_input_report::InputReport report;
  report.report = mouse;

  // Convert to FIDL and back.
  hid_input_report::FidlInputReport fidl_report = {};
  ASSERT_OK(SetFidlInputReport(report, &fidl_report));

  hid_input_report::InputReport new_input_report =
      hid_input_report::ToInputReport(fidl_report.builder.build());
  auto new_mouse = std::get<hid_input_report::MouseInputReport>(new_input_report.report);

  // Check the fields.
  ASSERT_EQ(mouse.movement_x, new_mouse.movement_x);
  ASSERT_EQ(mouse.movement_y, new_mouse.movement_y);

  ASSERT_EQ(mouse.num_buttons_pressed, new_mouse.num_buttons_pressed);
  for (size_t i = 0; i < mouse.num_buttons_pressed; i++) {
    ASSERT_EQ(mouse.buttons_pressed[i], new_mouse.buttons_pressed[i]);
  }
}

TEST(FidlTest, SensorInputDescriptor) {
  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::LINEAR_VELOCITY;
  axis.range.min = -126;
  axis.range.max = 126;

  hid_input_report::SensorDescriptor sensor_desc = {};
  sensor_desc.input = hid_input_report::SensorInputDescriptor();
  sensor_desc.input->values[0].axis = axis;
  sensor_desc.input->values[0].type = fuchsia_input_report::SensorType::ACCELEROMETER_X;

  axis.unit = fuchsia_input_report::Unit::LUX;
  sensor_desc.input->values[1].axis = axis;
  sensor_desc.input->values[1].type = fuchsia_input_report::SensorType::LIGHT_ILLUMINANCE;

  sensor_desc.input->num_values = 2;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = sensor_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.build();
  ASSERT_TRUE(fidl.has_sensor());
  ASSERT_TRUE(fidl.sensor().has_input());

  hid_input_report::SensorDescriptor new_desc = hid_input_report::ToSensorDescriptor(fidl.sensor());

  ASSERT_EQ(sensor_desc.input->num_values, new_desc.input->num_values);

  for (size_t i = 0; i < sensor_desc.input->num_values; i++) {
    TestAxis(sensor_desc.input->values[i].axis, new_desc.input->values[i].axis);
    ASSERT_EQ(sensor_desc.input->values[i].type, new_desc.input->values[i].type);
  }
}

TEST(FidlTest, SensorInputReport) {
  hid_input_report::SensorInputReport sensor_report = {};
  sensor_report.values[0] = 5;
  sensor_report.values[1] = -5;
  sensor_report.values[2] = 0xabcdef;
  sensor_report.num_values = 3;

  hid_input_report::InputReport report;
  report.report = sensor_report;

  hid_input_report::FidlInputReport fidl_report = {};
  ASSERT_OK(SetFidlInputReport(report, &fidl_report));

  hid_input_report::InputReport new_input_report =
      hid_input_report::ToInputReport(fidl_report.builder.build());
  auto new_sensor = std::get<hid_input_report::SensorInputReport>(new_input_report.report);

  ASSERT_EQ(new_sensor.num_values, sensor_report.num_values);
  for (size_t i = 0; i < sensor_report.num_values; i++) {
    ASSERT_EQ(new_sensor.values[i], sensor_report.values[i]);
  }
}

TEST(FidlTest, TouchInputDescriptor) {
  hid_input_report::TouchDescriptor touch_desc = {};
  touch_desc.input = hid_input_report::TouchInputDescriptor();
  touch_desc.input->touch_type = fuchsia_input_report::TouchType::TOUCHSCREEN;

  touch_desc.input->max_contacts = 100;

  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::DISTANCE;
  axis.range.min = 0;
  axis.range.max = 0xabcdef;

  touch_desc.input->contacts[0].position_x = axis;
  touch_desc.input->contacts[0].position_y = axis;

  axis.unit = fuchsia_input_report::Unit::PRESSURE;
  axis.range.min = 0;
  axis.range.max = 100;
  touch_desc.input->contacts[0].pressure = axis;

  touch_desc.input->num_contacts = 1;

  touch_desc.input->num_buttons = 2;
  touch_desc.input->buttons[0] = 1;
  touch_desc.input->buttons[1] = 2;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = touch_desc;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(desc, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.build();
  ASSERT_TRUE(fidl.has_touch());
  ASSERT_TRUE(fidl.touch().has_input());

  hid_input_report::TouchDescriptor new_desc = hid_input_report::ToTouchDescriptor(fidl.touch());

  ASSERT_EQ(touch_desc.input->max_contacts, new_desc.input->max_contacts);
  ASSERT_EQ(touch_desc.input->touch_type, new_desc.input->touch_type);
  ASSERT_EQ(touch_desc.input->num_contacts, new_desc.input->num_contacts);

  TestAxis(*touch_desc.input->contacts[0].position_x, *new_desc.input->contacts[0].position_x);
  TestAxis(*touch_desc.input->contacts[0].position_y, *new_desc.input->contacts[0].position_y);
  TestAxis(*touch_desc.input->contacts[0].pressure, *new_desc.input->contacts[0].pressure);

  ASSERT_EQ(touch_desc.input->num_buttons, new_desc.input->num_buttons);
  for (size_t i = 0; i < touch_desc.input->num_buttons; i++) {
    ASSERT_EQ(touch_desc.input->buttons[i], new_desc.input->buttons[i]);
  }
}

TEST(FidlTest, TouchInputReport) {
  hid_input_report::TouchInputReport touch_report = {};

  touch_report.num_contacts = 1;

  touch_report.contacts[0].position_x = 123;
  touch_report.contacts[0].position_y = 234;
  touch_report.contacts[0].pressure = 345;
  touch_report.contacts[0].contact_width = 678;
  touch_report.contacts[0].contact_height = 789;
  touch_report.num_pressed_buttons = 2;
  touch_report.pressed_buttons[0] = 123;
  touch_report.pressed_buttons[1] = 234;

  hid_input_report::InputReport report;
  report.report = touch_report;

  hid_input_report::FidlInputReport fidl_report = {};
  ASSERT_OK(SetFidlInputReport(report, &fidl_report));

  hid_input_report::InputReport new_input_report =
      hid_input_report::ToInputReport(fidl_report.builder.build());
  auto new_touch = std::get<hid_input_report::TouchInputReport>(new_input_report.report);

  ASSERT_EQ(touch_report.num_contacts, new_touch.num_contacts);

  EXPECT_EQ(touch_report.contacts[0].position_x, new_touch.contacts[0].position_x);
  EXPECT_EQ(touch_report.contacts[0].position_y, new_touch.contacts[0].position_y);
  EXPECT_EQ(touch_report.contacts[0].pressure, new_touch.contacts[0].pressure);
  EXPECT_EQ(touch_report.contacts[0].contact_width, new_touch.contacts[0].contact_width);
  EXPECT_EQ(touch_report.contacts[0].contact_height, new_touch.contacts[0].contact_height);
  EXPECT_EQ(touch_report.num_pressed_buttons, new_touch.num_pressed_buttons);
  for (size_t i = 0; i < touch_report.num_pressed_buttons; i++) {
    EXPECT_EQ(touch_report.pressed_buttons[i], new_touch.pressed_buttons[i]);
  }
}

TEST(FidlTest, KeyboardInputDescriptor) {
  hid_input_report::KeyboardDescriptor keyboard_descriptor = {};
  keyboard_descriptor.input = hid_input_report::KeyboardInputDescriptor();
  keyboard_descriptor.input->num_keys = 3;
  keyboard_descriptor.input->keys[0] = llcpp::fuchsia::ui::input2::Key::A;
  keyboard_descriptor.input->keys[1] = llcpp::fuchsia::ui::input2::Key::END;
  keyboard_descriptor.input->keys[2] = llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT;

  hid_input_report::ReportDescriptor descriptor;
  descriptor.descriptor = keyboard_descriptor;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(descriptor, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.build();
  ASSERT_TRUE(fidl.has_keyboard());
  ASSERT_TRUE(fidl.keyboard().has_input());

  hid_input_report::KeyboardDescriptor new_desc =
      hid_input_report::ToKeyboardDescriptor(fidl.keyboard());

  ASSERT_EQ(keyboard_descriptor.input->num_keys, new_desc.input->num_keys);
  for (size_t i = 0; i < keyboard_descriptor.input->num_keys; i++) {
    ASSERT_EQ(keyboard_descriptor.input->keys[i], new_desc.input->keys[i]);
  }
}

TEST(FidlTest, KeyboardOutputDescriptor) {
  hid_input_report::KeyboardDescriptor keyboard_descriptor = {};
  keyboard_descriptor.output = hid_input_report::KeyboardOutputDescriptor();
  keyboard_descriptor.output->num_leds = 3;
  keyboard_descriptor.output->leds[0] = fuchsia_input_report::LedType::NUM_LOCK;
  keyboard_descriptor.output->leds[1] = fuchsia_input_report::LedType::CAPS_LOCK;
  keyboard_descriptor.output->leds[2] = fuchsia_input_report::LedType::SCROLL_LOCK;

  hid_input_report::ReportDescriptor descriptor;
  descriptor.descriptor = keyboard_descriptor;

  hid_input_report::FidlDescriptor fidl_desc = {};
  ASSERT_OK(SetFidlDescriptor(descriptor, &fidl_desc));

  fuchsia_input_report::DeviceDescriptor fidl = fidl_desc.builder.build();
  ASSERT_TRUE(fidl.has_keyboard());
  ASSERT_TRUE(fidl.keyboard().has_output());
  auto& fidl_keyboard = fidl.keyboard().output();

  ASSERT_EQ(3, fidl_keyboard.leds().count());
  EXPECT_EQ(fuchsia_input_report::LedType::NUM_LOCK, fidl_keyboard.leds()[0]);
  EXPECT_EQ(fuchsia_input_report::LedType::CAPS_LOCK, fidl_keyboard.leds()[1]);
  EXPECT_EQ(fuchsia_input_report::LedType::SCROLL_LOCK, fidl_keyboard.leds()[2]);
}

TEST(FidlTest, KeyboardInputReport) {
  hid_input_report::KeyboardInputReport keyboard_report = {};
  keyboard_report.num_pressed_keys = 3;
  keyboard_report.pressed_keys[0] = llcpp::fuchsia::ui::input2::Key::A;
  keyboard_report.pressed_keys[1] = llcpp::fuchsia::ui::input2::Key::END;
  keyboard_report.pressed_keys[2] = llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT;

  hid_input_report::InputReport report;
  report.report = keyboard_report;

  hid_input_report::FidlInputReport fidl_report = {};
  ASSERT_OK(SetFidlInputReport(report, &fidl_report));

  hid_input_report::InputReport new_input_report =
      hid_input_report::ToInputReport(fidl_report.builder.build());
  auto new_keyboard = std::get<hid_input_report::KeyboardInputReport>(new_input_report.report);

  ASSERT_EQ(keyboard_report.num_pressed_keys, new_keyboard.num_pressed_keys);
  for (size_t i = 0; i < keyboard_report.num_pressed_keys; i++) {
    ASSERT_EQ(keyboard_report.pressed_keys[i], new_keyboard.pressed_keys[i]);
  }
}
