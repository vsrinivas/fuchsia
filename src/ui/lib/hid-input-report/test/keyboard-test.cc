// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/keyboard.h"

#include <fuchsia/ui/input2/cpp/fidl.h>

#include <variant>

#include <hid/boot.h>
#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "src/ui/lib/hid-input-report/device.h"
#include "src/ui/lib/key_util/key_util.h"

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

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.
TEST(KeyboardTest, BootKeyboard) {
  size_t descriptor_size;
  const uint8_t* boot_keyboard_descriptor = get_boot_kbd_report_desc(&descriptor_size);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_keyboard_descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::Keyboard keyboard;

  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = keyboard.GetDescriptor();

  hid_input_report::KeyboardDescriptor* keyboard_descriptor =
      std::get_if<hid_input_report::KeyboardDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(keyboard_descriptor);

  EXPECT_EQ(keyboard_descriptor->input->num_keys, 105U);

  // Test a report parses correctly.
  hid_boot_kbd_report kbd_report = {};
  kbd_report.modifier = HID_KBD_MODIFIER_LEFT_SHIFT | HID_KBD_MODIFIER_RIGHT_GUI;
  kbd_report.usage[0] = HID_USAGE_KEY_A;
  kbd_report.usage[1] = HID_USAGE_KEY_NON_US_BACKSLASH;
  kbd_report.usage[2] = HID_USAGE_KEY_UP;

  hid_input_report::InputReport report = {};
  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            keyboard.ParseInputReport(reinterpret_cast<uint8_t*>(&kbd_report), sizeof(kbd_report),
                                      &report));

  hid_input_report::KeyboardInputReport* keyboard_report =
      std::get_if<hid_input_report::KeyboardInputReport>(&report.report);
  ASSERT_NOT_NULL(keyboard_report);
  ASSERT_EQ(keyboard_report->num_pressed_keys, 5U);
  EXPECT_EQ(keyboard_report->pressed_keys[0], llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT);
  EXPECT_EQ(keyboard_report->pressed_keys[1], llcpp::fuchsia::ui::input2::Key::RIGHT_META);
  EXPECT_EQ(keyboard_report->pressed_keys[2], llcpp::fuchsia::ui::input2::Key::A);
  EXPECT_EQ(keyboard_report->pressed_keys[3], llcpp::fuchsia::ui::input2::Key::NON_US_BACKSLASH);
  EXPECT_EQ(keyboard_report->pressed_keys[4], llcpp::fuchsia::ui::input2::Key::UP);
}

TEST(KeyboardTest, OutputDescriptor) {
  size_t descriptor_size;
  const uint8_t* boot_keyboard_descriptor = get_boot_kbd_report_desc(&descriptor_size);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_keyboard_descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::Keyboard keyboard;

  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = keyboard.GetDescriptor();

  hid_input_report::KeyboardDescriptor* keyboard_descriptor =
      std::get_if<hid_input_report::KeyboardDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(keyboard_descriptor);
  ASSERT_TRUE(keyboard_descriptor->output);

  ASSERT_EQ(keyboard_descriptor->output->num_leds, 5);
  EXPECT_EQ(keyboard_descriptor->output->leds[0],
            hid_input_report::fuchsia_input_report::LedType::NUM_LOCK);
  EXPECT_EQ(keyboard_descriptor->output->leds[1],
            hid_input_report::fuchsia_input_report::LedType::CAPS_LOCK);
  EXPECT_EQ(keyboard_descriptor->output->leds[2],
            hid_input_report::fuchsia_input_report::LedType::SCROLL_LOCK);
  EXPECT_EQ(keyboard_descriptor->output->leds[3],
            hid_input_report::fuchsia_input_report::LedType::COMPOSE);
  EXPECT_EQ(keyboard_descriptor->output->leds[4],
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

  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = keyboard.GetDescriptor();

  hid_input_report::KeyboardDescriptor* keyboard_descriptor =
      std::get_if<hid_input_report::KeyboardDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(keyboard_descriptor);

  EXPECT_EQ(keyboard_descriptor->input->num_keys, 105U);

  // Test that all of the expected keys are here.
  for (size_t i = 0; i < 97; i++) {
    EXPECT_EQ(*key_util::fuchsia_key_to_hid_key(
                  static_cast<fuchsia::ui::input2::Key>(keyboard_descriptor->input->keys[i])),
              i + 4);
  }

  for (size_t i = 97; i < 105; i++) {
    EXPECT_EQ(*key_util::fuchsia_key_to_hid_key(
                  static_cast<fuchsia::ui::input2::Key>(keyboard_descriptor->input->keys[i])),
              0xE0 + (i - 97));
  }
}

TEST(KeyboardTest, BootKeyboardOutputReport) {
  size_t descriptor_size;
  const uint8_t* boot_keyboard_descriptor = get_boot_kbd_report_desc(&descriptor_size);
  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_keyboard_descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  hid_input_report::Keyboard keyboard;
  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            keyboard.ParseReportDescriptor(dev_desc->report[0]));
  std::array<hid_input_report::fuchsia_input_report::LedType, 2> led_array;
  led_array[0] = hid_input_report::fuchsia_input_report::LedType::NUM_LOCK;
  led_array[1] = hid_input_report::fuchsia_input_report::LedType::SCROLL_LOCK;
  // Build the FIDL table.
  auto led_view = fidl::VectorView<hid_input_report::fuchsia_input_report::LedType>(led_array);
  auto keyboard_builder = hid_input_report::fuchsia_input_report::KeyboardOutputReport::Build();
  keyboard_builder.set_enabled_leds(&led_view);
  hid_input_report::fuchsia_input_report::KeyboardOutputReport fidl_keyboard =
      keyboard_builder.view();
  auto builder = hid_input_report::fuchsia_input_report::OutputReport::Build();
  builder.set_keyboard(&fidl_keyboard);
  hid_input_report::fuchsia_input_report::OutputReport fidl_report = builder.view();
  uint8_t report_data;
  size_t out_report_size;
  auto result =
      keyboard.SetOutputReport(&fidl_report, &report_data, sizeof(report_data), &out_report_size);
  ASSERT_EQ(result, hid_input_report::kParseOk);
  ASSERT_EQ(1, out_report_size);
  ASSERT_EQ(0b101, report_data);
}
