// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <hid/usages.h>

#include "garnet/bin/ui/input_reader/input_reader.h"
#include "garnet/bin/ui/input_reader/tests/mock_device_watcher.h"
#include "garnet/bin/ui/input_reader/tests/mock_hid_decoder.h"
#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/tests/mocks/mock_input_device.h"
#include "lib/ui/tests/mocks/mock_input_device_registry.h"

namespace mozart {

namespace {

using MockInputDevice = mozart::test::MockInputDevice;
using MockInputDeviceRegistry = mozart::test::MockInputDeviceRegistry;

fxl::WeakPtr<MockDeviceWatcher> StartInputReader(InputReader* input_reader) {
  auto device_watcher = std::make_unique<MockDeviceWatcher>();
  auto weak_device_watcher = device_watcher->GetWeakPtr();
  input_reader->Start(std::move(device_watcher));
  return weak_device_watcher;
}

template <class... Args>
fxl::WeakPtr<MockHidDecoder> AddDevice(
    fxl::WeakPtr<MockDeviceWatcher> device_watcher, Args&&... args) {
  auto device = std::make_unique<MockHidDecoder>(std::forward<Args>(args)...);
  auto weak_device = device->GetWeakPtr();
  device_watcher->AddDevice(std::move(device));
  return weak_device;
}

}  // namespace

using ReaderInterpreterTest = ::gtest::TestLoopFixture;

TEST_F(ReaderInterpreterTest, RegisterKeyboardTest) {
  int registration_count = 0;

  MockInputDeviceRegistry registry(
      [&](MockInputDevice* client_device) {
        EXPECT_EQ(0, registration_count++);
        EXPECT_TRUE(client_device->descriptor()->keyboard);
      },
      nullptr);
  InputReader input_reader(&registry);

  fxl::WeakPtr<MockDeviceWatcher> device_watcher =
      StartInputReader(&input_reader);

  bool did_init = false;
  AddDevice(device_watcher, [&] {
    did_init = true;
    return std::make_pair<HidDecoder::Protocol, bool>(
        HidDecoder::Protocol::Keyboard, true);
  });
  EXPECT_TRUE(did_init);
  EXPECT_EQ(1, registration_count);
}

TEST_F(ReaderInterpreterTest, InputKeyboardTest) {
  int report_count = 0;
  fuchsia::ui::input::InputReport last_report;

  MockInputDeviceRegistry registry(nullptr,
                                   [&](fuchsia::ui::input::InputReport report) {
                                     ++report_count;
                                     last_report = std::move(report);
                                   });
  InputReader input_reader(&registry);

  fxl::WeakPtr<MockDeviceWatcher> device_watcher =
      StartInputReader(&input_reader);

  fxl::WeakPtr<MockHidDecoder> device =
      AddDevice(device_watcher, HidDecoder::Protocol::Keyboard);

  RunLoopUntilIdle();
  EXPECT_EQ(0, report_count);

  // A keyboard report is 8 bytes long, with bytes 3-8 containing HID usage
  // codes.
  device->Send({0, 0, HID_USAGE_KEY_A, 0, 0, 0, 0, 0}, 8);

  RunLoopUntilIdle();
  EXPECT_EQ(1, report_count);
  ASSERT_TRUE(last_report.keyboard);
  EXPECT_EQ(std::vector<uint32_t>{HID_USAGE_KEY_A},
            *last_report.keyboard->pressed_keys);

  device->Send({0, 0, HID_USAGE_KEY_A, HID_USAGE_KEY_Z, 0, 0, 0, 0}, 8);
  RunLoopUntilIdle();
  EXPECT_EQ(2, report_count);
  EXPECT_EQ(std::multiset<uint32_t>({HID_USAGE_KEY_A, HID_USAGE_KEY_Z}),
            std::multiset<uint32_t>(last_report.keyboard->pressed_keys->begin(),
                                    last_report.keyboard->pressed_keys->end()));

  device->Send({0, 0, HID_USAGE_KEY_Z, 0, 0, 0, 0, 0}, 8);
  RunLoopUntilIdle();
  EXPECT_EQ(std::vector<uint32_t>{HID_USAGE_KEY_Z},
            *last_report.keyboard->pressed_keys);
}

TEST_F(ReaderInterpreterTest, RemoveKeyboardTest) {
  MockInputDeviceRegistry registry(nullptr, nullptr);
  InputReader input_reader(&registry);
  fxl::WeakPtr<MockDeviceWatcher> device_watcher =
      StartInputReader(&input_reader);
  fxl::WeakPtr<MockHidDecoder> device =
      AddDevice(device_watcher, HidDecoder::Protocol::Keyboard);

  device->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(device);
}

}  // namespace mozart
