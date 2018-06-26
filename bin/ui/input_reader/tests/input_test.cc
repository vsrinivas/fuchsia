// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid/usages.h>

#include "gtest/gtest.h"
#include "lib/fxl/time/time_point.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/tests/mocks/mock_input_device.h"
#include "lib/ui/tests/mocks/mock_input_device_registry.h"

namespace input {
namespace test {

using InputTest = ::gtest::TestLoopFixture;

fuchsia::ui::input::DeviceDescriptor GenerateKeyboardDescriptor() {
  fuchsia::ui::input::KeyboardDescriptorPtr keyboard =
      fuchsia::ui::input::KeyboardDescriptor::New();
  keyboard->keys.resize(HID_USAGE_KEY_RIGHT_GUI - HID_USAGE_KEY_A);
  for (size_t index = HID_USAGE_KEY_A; index < HID_USAGE_KEY_RIGHT_GUI;
       ++index) {
    keyboard->keys->at(index - HID_USAGE_KEY_A) = index;
  }
  fuchsia::ui::input::DeviceDescriptor descriptor;
  descriptor.keyboard = std::move(keyboard);
  return descriptor;
}

TEST_F(InputTest, RegisterKeyboardTest) {
  fuchsia::ui::input::DeviceDescriptor descriptor =
      GenerateKeyboardDescriptor();

  fuchsia::ui::input::InputDevicePtr input_device;
  uint32_t on_register_count = 0;
  mozart::test::MockInputDeviceRegistry registry(
      [&on_register_count](mozart::test::MockInputDevice* input_device) {
        on_register_count++;
      },
      nullptr);

  registry.RegisterDevice(std::move(descriptor), input_device.NewRequest());

  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_register_count);
}

TEST_F(InputTest, InputKeyboardTest) {
  fuchsia::ui::input::DeviceDescriptor descriptor =
      GenerateKeyboardDescriptor();

  fuchsia::ui::input::InputDevicePtr input_device;
  uint32_t on_report_count = 0;
  mozart::test::MockInputDeviceRegistry registry(
      nullptr, [&on_report_count](fuchsia::ui::input::InputReport report) {
        EXPECT_TRUE(report.keyboard);
        EXPECT_EQ(HID_USAGE_KEY_A, report.keyboard->pressed_keys->at(0));
        on_report_count++;
      });

  registry.RegisterDevice(std::move(descriptor), input_device.NewRequest());

  // PRESSED
  fuchsia::ui::input::KeyboardReportPtr keyboard_report =
      fuchsia::ui::input::KeyboardReport::New();
  keyboard_report->pressed_keys.push_back(HID_USAGE_KEY_A);

  fuchsia::ui::input::InputReport report;
  report.event_time = fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
  report.keyboard = std::move(keyboard_report);
  input_device->DispatchReport(std::move(report));

  RunLoopUntilIdle();
  EXPECT_EQ(1u, on_report_count);
}

}  // namespace test
}  // namespace input
