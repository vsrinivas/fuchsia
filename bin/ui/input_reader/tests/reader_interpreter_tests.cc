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

// This fixture sets up a |MockDeviceWatcher| so that tests can add mock
// devices.
class ReaderInterpreterTest : public gtest::TestLoopFixture {
 protected:
  // Starts an |InputReader| with a |MockDeviceWatcher| and saves it locally so
  // that |MockHidDecoder|s can be added to it.
  void StartInputReader(InputReader* input_reader) {
    auto device_watcher = std::make_unique<MockDeviceWatcher>();
    device_watcher_ = device_watcher->GetWeakPtr();
    input_reader->Start(std::move(device_watcher));
  }

  // Creates a |MockHidDecoder| with the supplied |args| and adds it to the
  // |MockDeviceWatcher|, returning an |fxl::WeakPtr| to the new
  // |MockHidDecoder|.
  template <class... Args>
  fxl::WeakPtr<MockHidDecoder> AddDevice(Args&&... args) {
    auto device = std::make_unique<MockHidDecoder>(std::forward<Args>(args)...);
    auto weak_device = device->GetWeakPtr();
    device_watcher_->AddDevice(std::move(device));
    return weak_device;
  }

 private:
  fxl::WeakPtr<MockDeviceWatcher> device_watcher_;
};

// This fixture sets up a |MockInputDeviceRegistry| and an |InputReader| in
// addition to the |MockDeviceWatcher| provided by |ReaderInterpreterTest| so
// that tests can additionally verify the reports seen by the registry.
class ReaderInterpreterInputTest : public ReaderInterpreterTest {
 protected:
  ReaderInterpreterInputTest()
      : registry_(nullptr,
                  [this](fuchsia::ui::input::InputReport report) {
                    ++report_count_;
                    last_report_ = std::move(report);
                  }),
        input_reader_(&registry_) {}

  int report_count_ = 0;
  fuchsia::ui::input::InputReport last_report_;

 private:
  void SetUp() override { StartInputReader(&input_reader_); }

  MockInputDeviceRegistry registry_;
  InputReader input_reader_;
};

}  // namespace

TEST_F(ReaderInterpreterTest, RegisterKeyboardTest) {
  int registration_count = 0;

  MockInputDeviceRegistry registry(
      [&](MockInputDevice* client_device) {
        EXPECT_EQ(0, registration_count++);
        EXPECT_TRUE(client_device->descriptor()->keyboard);
      },
      nullptr);
  InputReader input_reader(&registry);

  StartInputReader(&input_reader);

  bool did_init = false;
  AddDevice([&] {
    did_init = true;
    return std::make_pair<HidDecoder::Protocol, bool>(
        HidDecoder::Protocol::Keyboard, true);
  });
  EXPECT_TRUE(did_init);
  EXPECT_EQ(1, registration_count);
}

TEST_F(ReaderInterpreterTest, RemoveKeyboardTest) {
  MockInputDeviceRegistry registry(nullptr, nullptr);
  InputReader input_reader(&registry);
  StartInputReader(&input_reader);

  fxl::WeakPtr<MockHidDecoder> device =
      AddDevice(HidDecoder::Protocol::Keyboard);

  device->Close();
  RunLoopUntilIdle();
  EXPECT_FALSE(device);
}

TEST_F(ReaderInterpreterInputTest, KeyboardTest) {
  fxl::WeakPtr<MockHidDecoder> device =
      AddDevice(HidDecoder::Protocol::Keyboard);

  RunLoopUntilIdle();
  EXPECT_EQ(0, report_count_);

  // A keyboard report is 8 bytes long, with bytes 3-8 containing HID usage
  // codes.
  device->Send({0, 0, HID_USAGE_KEY_A, 0, 0, 0, 0, 0}, 8);

  RunLoopUntilIdle();
  EXPECT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.keyboard);
  EXPECT_EQ(std::vector<uint32_t>{HID_USAGE_KEY_A},
            last_report_.keyboard->pressed_keys);

  device->Send({0, 0, HID_USAGE_KEY_A, HID_USAGE_KEY_Z, 0, 0, 0, 0}, 8);
  RunLoopUntilIdle();
  EXPECT_EQ(2, report_count_);
  EXPECT_EQ(
      std::multiset<uint32_t>({HID_USAGE_KEY_A, HID_USAGE_KEY_Z}),
      std::multiset<uint32_t>(last_report_.keyboard->pressed_keys.begin(),
                              last_report_.keyboard->pressed_keys.end()));

  device->Send({0, 0, HID_USAGE_KEY_Z, 0, 0, 0, 0, 0}, 8);
  RunLoopUntilIdle();
  EXPECT_EQ(std::vector<uint32_t>{HID_USAGE_KEY_Z},
            last_report_.keyboard->pressed_keys);
}

TEST_F(ReaderInterpreterInputTest, LightSensorTest) {
  fxl::WeakPtr<MockHidDecoder> device =
      AddDevice(HidDecoder::Protocol::LightSensor);

  RunLoopUntilIdle();
  EXPECT_EQ(0, report_count_);

  {
    HidDecoder::HidAmbientLightSimple light{/* int16_t illuminance */ 42};
    device->Send(light);
  }

  RunLoopUntilIdle();
  EXPECT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.sensor);
  EXPECT_TRUE(last_report_.sensor->is_scalar());
  EXPECT_EQ(42, last_report_.sensor->scalar());
}

}  // namespace mozart
