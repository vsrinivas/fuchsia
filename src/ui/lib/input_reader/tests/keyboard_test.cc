// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/keyboard.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include <gtest/gtest.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/boot.h>
#include <hid/usages.h>

namespace input {

namespace test {

namespace {

// This is a keyboard with multiple keys of the same usage.
const uint8_t double_keys_keyboard[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,  //   Usage Minimum (0xE0)
    0x29, 0xE7,  //   Usage Maximum (0xE7)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x19, 0xE0,  //   Usage Minimum (0xE0)
    0x29, 0xE7,  //   Usage Maximum (0xE7)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x02,  //   Report Count (2)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,  //   Usage Minimum (0x00)
    0x29, 0x65,  //   Usage Maximum (0x65)
    0x81, 0x00,  //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,        // End Collection
};

}  // namespace

// This test double checks that we don't double count keys that are included twice.
TEST(KeyboardTest, DoubleCountingKeys) {
  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res =
      hid::ParseReportDescriptor(double_keys_keyboard, sizeof(double_keys_keyboard), &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  // Test the descriptor parses correctly.
  ui_input::Keyboard keyboard = {};
  ui_input::Device::Descriptor device_descriptor = {};
  bool success = keyboard.ParseReportDescriptor(dev_desc->report[0], &device_descriptor);
  ASSERT_TRUE(success);
  ASSERT_TRUE(device_descriptor.has_keyboard);

  EXPECT_EQ(device_descriptor.keyboard_descriptor->keys.size(), 109U);

  // Test that all of the expected keys are here.
  for (size_t i = 0; i < 101; i++) {
    EXPECT_EQ(device_descriptor.keyboard_descriptor->keys[i], i);
  }

  for (size_t i = 101; i < 109; i++) {
    EXPECT_EQ(device_descriptor.keyboard_descriptor->keys[i], 0xE0 + (i - 101));
  }
}

TEST(KeyboardTest, BootKeyboard) {
  size_t descriptor_size;
  const uint8_t* boot_keyboard_descriptor = get_boot_kbd_report_desc(&descriptor_size);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_keyboard_descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  // Test the descriptor parses correctly.
  ui_input::Keyboard keyboard = {};
  ui_input::Device::Descriptor device_descriptor = {};
  bool success = keyboard.ParseReportDescriptor(dev_desc->report[0], &device_descriptor);
  ASSERT_TRUE(success);
  ASSERT_TRUE(device_descriptor.has_keyboard);
  EXPECT_EQ(device_descriptor.keyboard_descriptor->keys.size(), 109U);

  // Test a report parses correctly.
  hid_boot_kbd_report kbd_report = {};
  kbd_report.modifier = HID_KBD_MODIFIER_LEFT_SHIFT | HID_KBD_MODIFIER_RIGHT_GUI;
  kbd_report.usage[0] = HID_USAGE_KEY_A;
  kbd_report.usage[1] = HID_USAGE_KEY_NON_US_BACKSLASH;
  kbd_report.usage[2] = HID_USAGE_KEY_UP;

  fuchsia::ui::input::InputReport report;
  report.keyboard = fuchsia::ui::input::KeyboardReport::New();
  success =
      keyboard.ParseReport(reinterpret_cast<uint8_t*>(&kbd_report), sizeof(kbd_report), &report);
  ASSERT_EQ(true, success);

  ASSERT_EQ(report.keyboard->pressed_keys.size(), 5U);
  EXPECT_EQ(report.keyboard->pressed_keys[0], HID_USAGE_KEY_LEFT_SHIFT);
  EXPECT_EQ(report.keyboard->pressed_keys[1], HID_USAGE_KEY_RIGHT_GUI);
  EXPECT_EQ(report.keyboard->pressed_keys[2], HID_USAGE_KEY_A);
  EXPECT_EQ(report.keyboard->pressed_keys[3], HID_USAGE_KEY_NON_US_BACKSLASH);
  EXPECT_EQ(report.keyboard->pressed_keys[4], HID_USAGE_KEY_UP);
}

}  // namespace test
}  // namespace input
