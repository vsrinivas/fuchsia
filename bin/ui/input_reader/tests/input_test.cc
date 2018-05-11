// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/usages.h>
#include <input/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/gtest/test_with_message_loop.h"
#include "lib/ui/tests/mocks/mock_input_device.h"
#include "lib/ui/tests/mocks/mock_input_device_registry.h"

namespace input {
namespace test {

using InputTest = ::gtest::TestWithMessageLoop;

input::DeviceDescriptor GenerateKeyboardDescriptor() {
  input::KeyboardDescriptorPtr keyboard = input::KeyboardDescriptor::New();
  keyboard->keys.resize(HID_USAGE_KEY_RIGHT_GUI - HID_USAGE_KEY_A);
  for (size_t index = HID_USAGE_KEY_A; index < HID_USAGE_KEY_RIGHT_GUI;
       ++index) {
    keyboard->keys->at(index - HID_USAGE_KEY_A) = index;
  }
  input::DeviceDescriptor descriptor;
  descriptor.keyboard = std::move(keyboard);
  return descriptor;
}

TEST_F(InputTest, RegisterKeyboardTest) {
  input::DeviceDescriptor descriptor = GenerateKeyboardDescriptor();

  input::InputDevicePtr input_device;
  uint32_t on_register_count = 0;
  mozart::test::MockInputDeviceRegistry registry(
      [&on_register_count](mozart::test::MockInputDevice* input_device) {
        on_register_count++;
      },
      nullptr);

  registry.RegisterDevice(std::move(descriptor), input_device.NewRequest());

  EXPECT_TRUE(RunLoopUntilWithTimeout(
      [&on_register_count]() -> bool { return on_register_count == 1u; }));
}

TEST_F(InputTest, InputKeyboardTest) {
  input::DeviceDescriptor descriptor = GenerateKeyboardDescriptor();

  input::InputDevicePtr input_device;
  uint32_t on_report_count = 0;
  mozart::test::MockInputDeviceRegistry registry(
      nullptr, [&on_report_count](input::InputReport report) {
        EXPECT_TRUE(report.keyboard);
        EXPECT_EQ(HID_USAGE_KEY_A, report.keyboard->pressed_keys->at(0));
        on_report_count++;
      });

  registry.RegisterDevice(std::move(descriptor), input_device.NewRequest());

  // PRESSED
  input::KeyboardReportPtr keyboard_report = input::KeyboardReport::New();
  keyboard_report->pressed_keys.push_back(HID_USAGE_KEY_A);

  input::InputReport report;
  report.event_time = fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
  report.keyboard = std::move(keyboard_report);
  input_device->DispatchReport(std::move(report));

  EXPECT_TRUE(RunLoopUntilWithTimeout(
      [&on_report_count]() -> bool { return on_report_count == 1u; }));
}

}  // namespace test
}  // namespace input
