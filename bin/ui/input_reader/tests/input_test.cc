// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/usages.h>

#include "gtest/gtest.h"

#include "lib/ui/tests/mocks/mock_input_device.h"
#include "lib/ui/tests/mocks/mock_input_device_registry.h"
#include "lib/ui/tests/test_with_message_loop.h"
#include "lib/ui/input/fidl/input_reports.fidl.h"

namespace input {
namespace test {

class InputTest : public ::testing::Test {};

mozart::DeviceDescriptorPtr GenerateKeyboardDescriptor() {
  mozart::KeyboardDescriptorPtr keyboard = mozart::KeyboardDescriptor::New();
  keyboard->keys.resize(HID_USAGE_KEY_RIGHT_GUI - HID_USAGE_KEY_A);
  for (size_t index = HID_USAGE_KEY_A; index < HID_USAGE_KEY_RIGHT_GUI;
       ++index) {
    keyboard->keys[index - HID_USAGE_KEY_A] = index;
  }
  mozart::DeviceDescriptorPtr descriptor = mozart::DeviceDescriptor::New();
  descriptor->keyboard = std::move(keyboard);
  return descriptor;
}

TEST_F(InputTest, RegisterKeyboardTest) {
  mozart::DeviceDescriptorPtr descriptor = GenerateKeyboardDescriptor();

  mozart::InputDevicePtr input_device;
  uint32_t on_register_count = 0;
  mozart::test::MockInputDeviceRegistry registry(
      [&on_register_count](mozart::test::MockInputDevice* input_device) {
        on_register_count++;
      },
      nullptr);

  registry.RegisterDevice(std::move(descriptor), input_device.NewRequest());

  RUN_MESSAGE_LOOP_WHILE(on_register_count == 0);
  EXPECT_EQ(1u, on_register_count);
}

TEST_F(InputTest, InputKeyboardTest) {
  mozart::DeviceDescriptorPtr descriptor = GenerateKeyboardDescriptor();

  mozart::InputDevicePtr input_device;
  uint32_t on_report_count = 0;
  mozart::test::MockInputDeviceRegistry registry(
      nullptr, [&on_report_count](mozart::InputReportPtr report) {
        EXPECT_TRUE(report->keyboard);
        EXPECT_EQ(HID_USAGE_KEY_A, report->keyboard->pressed_keys[0]);
        on_report_count++;
      });

  registry.RegisterDevice(std::move(descriptor), input_device.NewRequest());

  // PRESSED
  mozart::KeyboardReportPtr keyboard_report = mozart::KeyboardReport::New();
  keyboard_report->pressed_keys.resize(1);
  keyboard_report->pressed_keys[0] = HID_USAGE_KEY_A;

  mozart::InputReportPtr report = mozart::InputReport::New();
  report->event_time = ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
  report->keyboard = std::move(keyboard_report);
  input_device->DispatchReport(std::move(report));

  RUN_MESSAGE_LOOP_WHILE(on_report_count == 0);
  EXPECT_EQ(1u, on_report_count);
}

}  // namespace test
}  // namespace input
