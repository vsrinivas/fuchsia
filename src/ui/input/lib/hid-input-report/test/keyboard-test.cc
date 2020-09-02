// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/keyboard.h"

#include <fuchsia/input/cpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>

#include <variant>

#include <hid/boot.h>
#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/device.h"
#include "src/ui/input/lib/hid-input-report/test/test.h"
#include "src/ui/lib/key_util/key_util.h"

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

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

// This keyboard declares keys up to 0xFF (256 keys).
const uint8_t full_keys_keyboard[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null
                       //   Position,Non-volatile)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null
                       //   Position,Non-volatile)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (0xFF)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
};

}  // namespace

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.
TEST(KeyboardTest, BootKeyboard) {
  size_t descriptor_size;
  const uint8_t* boot_keyboard_descriptor = get_boot_kbd_report_desc(&descriptor_size);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_keyboard_descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::Keyboard keyboard;

  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.CreateDescriptor(&descriptor_allocator, &descriptor_builder));
  fuchsia_input_report::DeviceDescriptor descriptor = descriptor_builder.build();
  EXPECT_TRUE(descriptor.has_keyboard());
  EXPECT_TRUE(descriptor.keyboard().has_input());
  EXPECT_TRUE(descriptor.keyboard().input().has_keys());
  EXPECT_EQ(105, descriptor.keyboard().input().keys().count());
  EXPECT_EQ(105, descriptor.keyboard().input().keys3().count());

  // Test a report parses correctly.
  hid_boot_kbd_report kbd_report = {};
  kbd_report.modifier = HID_KBD_MODIFIER_LEFT_SHIFT | HID_KBD_MODIFIER_RIGHT_GUI;
  kbd_report.usage[0] = HID_USAGE_KEY_A;
  kbd_report.usage[1] = HID_USAGE_KEY_NON_US_BACKSLASH;
  kbd_report.usage[2] = HID_USAGE_KEY_UP;

  hid_input_report::TestReportAllocator report_allocator;
  auto report_builder = fuchsia_input_report::InputReport::Builder(
      report_allocator.make<fuchsia_input_report::InputReport::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.ParseInputReport(reinterpret_cast<uint8_t*>(&kbd_report), sizeof(kbd_report),
                                      &report_allocator, &report_builder));

  fuchsia_input_report::InputReport input_report = report_builder.build();
  ASSERT_TRUE(input_report.has_keyboard());

  ASSERT_EQ(input_report.keyboard().pressed_keys().count(), 5U);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[0], llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[1], llcpp::fuchsia::ui::input2::Key::RIGHT_META);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[2], llcpp::fuchsia::ui::input2::Key::A);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[3],
            llcpp::fuchsia::ui::input2::Key::NON_US_BACKSLASH);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[4], llcpp::fuchsia::ui::input2::Key::UP);

  EXPECT_EQ(input_report.keyboard().pressed_keys3()[0], llcpp::fuchsia::input::Key::LEFT_SHIFT);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[1], llcpp::fuchsia::input::Key::RIGHT_META);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[2], llcpp::fuchsia::input::Key::A);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[3],
            llcpp::fuchsia::input::Key::NON_US_BACKSLASH);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[4], llcpp::fuchsia::input::Key::UP);
}

TEST(KeyboardTest, OutputDescriptor) {
  size_t descriptor_size;
  const uint8_t* boot_keyboard_descriptor = get_boot_kbd_report_desc(&descriptor_size);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_keyboard_descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::Keyboard keyboard;

  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.CreateDescriptor(&descriptor_allocator, &descriptor_builder));
  fuchsia_input_report::DeviceDescriptor descriptor = descriptor_builder.build();

  ASSERT_EQ(descriptor.keyboard().output().leds().count(), 5);
  EXPECT_EQ(descriptor.keyboard().output().leds()[0],
            hid_input_report::fuchsia_input_report::LedType::NUM_LOCK);
  EXPECT_EQ(descriptor.keyboard().output().leds()[1],
            hid_input_report::fuchsia_input_report::LedType::CAPS_LOCK);
  EXPECT_EQ(descriptor.keyboard().output().leds()[2],
            hid_input_report::fuchsia_input_report::LedType::SCROLL_LOCK);
  EXPECT_EQ(descriptor.keyboard().output().leds()[3],
            hid_input_report::fuchsia_input_report::LedType::COMPOSE);
  EXPECT_EQ(descriptor.keyboard().output().leds()[4],
            hid_input_report::fuchsia_input_report::LedType::KANA);
}

// This test double checks that we don't double count keys that are included twice.
TEST(KeyboardTest, DoubleCountingKeys) {
  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res =
      hid::ParseReportDescriptor(double_keys_keyboard, sizeof(double_keys_keyboard), &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  // Test the descriptor parses correctly.
  hid_input_report::Keyboard keyboard;

  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.CreateDescriptor(&descriptor_allocator, &descriptor_builder));
  fuchsia_input_report::DeviceDescriptor descriptor = descriptor_builder.build();

  EXPECT_EQ(descriptor.keyboard().input().keys().count(), 105U);
}

TEST(KeyboardTest, BootKeyboardOutputReport) {
  size_t descriptor_size;
  const uint8_t* boot_keyboard_descriptor = get_boot_kbd_report_desc(&descriptor_size);
  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_keyboard_descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  hid_input_report::Keyboard keyboard;
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));
  std::array<hid_input_report::fuchsia_input_report::LedType, 2> led_array;
  led_array[0] = hid_input_report::fuchsia_input_report::LedType::NUM_LOCK;
  led_array[1] = hid_input_report::fuchsia_input_report::LedType::SCROLL_LOCK;
  // Build the FIDL table.
  auto led_view = fidl::unowned_vec(led_array);
  hid_input_report::fuchsia_input_report::KeyboardOutputReport::UnownedBuilder keyboard_builder;
  keyboard_builder.set_enabled_leds(fidl::unowned_ptr(&led_view));
  hid_input_report::fuchsia_input_report::KeyboardOutputReport fidl_keyboard =
      keyboard_builder.build();
  hid_input_report::fuchsia_input_report::OutputReport::UnownedBuilder builder;
  builder.set_keyboard(fidl::unowned_ptr(&fidl_keyboard));
  hid_input_report::fuchsia_input_report::OutputReport fidl_report = builder.build();
  uint8_t report_data;
  size_t out_report_size;
  auto result =
      keyboard.SetOutputReport(&fidl_report, &report_data, sizeof(report_data), &out_report_size);
  ASSERT_EQ(result, hid_input_report::ParseResult::kOk);
  ASSERT_EQ(1, out_report_size);
  ASSERT_EQ(0b101, report_data);
}

TEST(KeyboardTest, FullKeysKeyboard) {
  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res =
      hid::ParseReportDescriptor(full_keys_keyboard, sizeof(full_keys_keyboard), &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::Keyboard keyboard;

  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.CreateDescriptor(&descriptor_allocator, &descriptor_builder));
  fuchsia_input_report::DeviceDescriptor descriptor = descriptor_builder.build();

  EXPECT_EQ(descriptor.keyboard().input().keys().count(), 107);
  EXPECT_EQ(descriptor.keyboard().input().keys3().count(), 107);

  // Test a report parses correctly.
  hid_boot_kbd_report kbd_report = {};
  kbd_report.modifier = HID_KBD_MODIFIER_LEFT_SHIFT | HID_KBD_MODIFIER_RIGHT_GUI;
  kbd_report.usage[0] = HID_USAGE_KEY_A;
  kbd_report.usage[1] = HID_USAGE_KEY_NON_US_BACKSLASH;
  kbd_report.usage[2] = HID_USAGE_KEY_UP;

  hid_input_report::TestReportAllocator report_allocator;
  auto report_builder = fuchsia_input_report::InputReport::Builder(
      report_allocator.make<fuchsia_input_report::InputReport::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            keyboard.ParseInputReport(reinterpret_cast<uint8_t*>(&kbd_report), sizeof(kbd_report),
                                      &report_allocator, &report_builder));
  fuchsia_input_report::InputReport input_report = report_builder.build();

  ASSERT_EQ(input_report.keyboard().pressed_keys().count(), 5U);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[0], llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[1], llcpp::fuchsia::ui::input2::Key::RIGHT_META);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[2], llcpp::fuchsia::ui::input2::Key::A);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[3],
            llcpp::fuchsia::ui::input2::Key::NON_US_BACKSLASH);
  EXPECT_EQ(input_report.keyboard().pressed_keys()[4], llcpp::fuchsia::ui::input2::Key::UP);

  ASSERT_EQ(input_report.keyboard().pressed_keys3().count(), 5U);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[0], llcpp::fuchsia::input::Key::LEFT_SHIFT);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[1], llcpp::fuchsia::input::Key::RIGHT_META);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[2], llcpp::fuchsia::input::Key::A);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[3],
            llcpp::fuchsia::input::Key::NON_US_BACKSLASH);
  EXPECT_EQ(input_report.keyboard().pressed_keys3()[4], llcpp::fuchsia::input::Key::UP);
}

TEST(KeyboardTest, DeviceType) {
  hid_input_report::Keyboard keyboard;
  ASSERT_EQ(hid_input_report::DeviceType::kKeyboard, keyboard.GetDeviceType());
}
