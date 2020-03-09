// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <set>

#include <ddk/device.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/input/lib/hid-input-report/fidl.h"
#include "src/ui/input/testing/fake_input_report_device/fake.h"
#include "src/ui/lib/input_report_reader/input_reader.h"
#include "src/ui/lib/input_report_reader/tests/mock_device_watcher.h"
#include "src/ui/testing/mock_input_device.h"
#include "src/ui/testing/mock_input_device_registry.h"

namespace ui_input {

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

namespace {

using MockInputDevice = ui_input::test::MockInputDevice;
using MockInputDeviceRegistry = ui_input::test::MockInputDeviceRegistry;

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

  void AddDevice(zx::channel chan) { device_watcher_->AddDevice(std::move(chan)); }

 private:
  fxl::WeakPtr<MockDeviceWatcher> device_watcher_;
};

// This fixture sets up a |MockInputDeviceRegistry| and an |InputReader| in
// addition to the |MockDeviceWatcher| provided by |ReaderInterpreterTest| so
// that tests can additionally verify the reports seen by the registry.
class ReaderInterpreterInputTest : public ReaderInterpreterTest {
 protected:
  ReaderInterpreterInputTest()
      : registry_([this](test::MockInputDevice* device) { last_device_ = device; },
                  [this](fuchsia::ui::input::InputReport report) {
                    ++report_count_;
                    last_report_ = std::move(report);
                  }),
        input_reader_(&registry_) {}

  int report_count_ = 0;
  fuchsia::ui::input::InputReport last_report_;
  MockInputDevice* last_device_ = nullptr;

 private:
  void SetUp() override { StartInputReader(&input_reader_); }

  MockInputDeviceRegistry registry_;
  InputReader input_reader_;
};

TEST_F(ReaderInterpreterInputTest, TouchScreen) {
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_device_;
  std::optional<fuchsia_input_report::InputDevice::SyncClient> client_;

  // Make the channels and the fake device.
  zx::channel token_server, token_client;
  ASSERT_EQ(zx::channel::create(0, &token_server, &token_client), ZX_OK);
  fake_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>();

  // Make and run the thread for the fake device's FIDL interface.
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop_->StartThread("test-print-input-report-loop"), ZX_OK);
  fidl::Bind(loop_->dispatcher(), std::move(token_server), fake_device_.get());
  auto cancel_loop = fbl::MakeAutoCall([&]() {
    loop_->Quit();
    loop_->JoinThreads();
  });

  // Add a touchscreen descriptor.
  {
    hid_input_report::TouchDescriptor touch_desc = {};
    touch_desc.input = hid_input_report::TouchInputDescriptor();
    touch_desc.input->touch_type = fuchsia_input_report::TouchType::TOUCHSCREEN;

    touch_desc.input->max_contacts = 100;

    fuchsia_input_report::Axis axis;
    axis.unit = fuchsia_input_report::Unit::NONE;
    axis.range.min = 0;
    axis.range.max = 300;

    touch_desc.input->contacts[0].position_x = axis;

    axis.range.max = 500;
    touch_desc.input->contacts[0].position_y = axis;

    touch_desc.input->num_contacts = 1;

    hid_input_report::ReportDescriptor desc;
    desc.descriptor = touch_desc;

    fake_device_->SetDescriptor(desc);
  }

  // Add the device.
  AddDevice(std::move(token_client));
  RunLoopUntilIdle();

  // Check the touchscreen descriptor.
  {
    ASSERT_NE(last_device_, nullptr);
    fuchsia::ui::input::DeviceDescriptor* descriptor = last_device_->descriptor();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->touchscreen, nullptr);
    ASSERT_EQ(descriptor->touchscreen->x.range.min, 0);
    ASSERT_EQ(descriptor->touchscreen->x.range.max, 300);
    ASSERT_EQ(descriptor->touchscreen->y.range.min, 0);
    ASSERT_EQ(descriptor->touchscreen->y.range.max, 500);
  }

  // Send a Touchscreen report.
  {
    hid_input_report::TouchInputReport touch = {};
    touch.num_contacts = 1;
    touch.contacts[0].contact_id = 10;
    touch.contacts[0].is_pressed = true;
    touch.contacts[0].position_x = 30;
    touch.contacts[0].position_y = 50;

    hid_input_report::InputReport report;
    report.report = touch;

    fake_device_->SetReport(report);
  }
  RunLoopUntilIdle();

  // Check the touchscreen report.
  {
    ASSERT_EQ(report_count_, 1);
    ASSERT_EQ(last_report_.touchscreen->touches.size(), 1U);
    ASSERT_EQ(last_report_.touchscreen->touches[0].finger_id, 10U);
    ASSERT_EQ(last_report_.touchscreen->touches[0].x, 30);
    ASSERT_EQ(last_report_.touchscreen->touches[0].y, 50);
  }
}

TEST_F(ReaderInterpreterInputTest, ConsumerControl) {
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_device_;
  std::optional<fuchsia_input_report::InputDevice::SyncClient> client_;

  // Make the channels and the fake device.
  zx::channel token_server, token_client;
  ASSERT_EQ(zx::channel::create(0, &token_server, &token_client), ZX_OK);
  fake_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>();

  // Make and run the thread for the fake device's FIDL interface.
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop_->StartThread("test-print-input-report-loop"), ZX_OK);
  fidl::Bind(loop_->dispatcher(), std::move(token_server), fake_device_.get());
  auto cancel_loop = fbl::MakeAutoCall([&]() {
    loop_->Quit();
    loop_->JoinThreads();
  });

  // Add a descriptor.
  {
    hid_input_report::ConsumerControlDescriptor consumer_desc = {};
    consumer_desc.input = hid_input_report::ConsumerControlInputDescriptor();
    consumer_desc.input->num_buttons = 5;
    consumer_desc.input->buttons[0] = fuchsia_input_report::ConsumerControlButton::VOLUME_UP;
    consumer_desc.input->buttons[1] = fuchsia_input_report::ConsumerControlButton::VOLUME_DOWN;
    consumer_desc.input->buttons[2] = fuchsia_input_report::ConsumerControlButton::PAUSE;
    consumer_desc.input->buttons[3] = fuchsia_input_report::ConsumerControlButton::MIC_MUTE;
    consumer_desc.input->buttons[4] = fuchsia_input_report::ConsumerControlButton::REBOOT;

    hid_input_report::ReportDescriptor desc;
    desc.descriptor = consumer_desc;

    fake_device_->SetDescriptor(desc);
  }

  // Add the device.
  AddDevice(std::move(token_client));
  RunLoopUntilIdle();

  // Check the descriptor.
  {
    ASSERT_NE(last_device_, nullptr);
    fuchsia::ui::input::DeviceDescriptor* descriptor = last_device_->descriptor();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->media_buttons, nullptr);
    ASSERT_EQ(descriptor->media_buttons->buttons,
              fuchsia::ui::input::kVolumeUp | fuchsia::ui::input::kVolumeDown |
                  fuchsia::ui::input::kPause | fuchsia::ui::input::kMicMute |
                  fuchsia::ui::input::kReset);
  }

  // Send a report.
  {
    hid_input_report::ConsumerControlInputReport consumer = {};
    consumer.num_pressed_buttons = 5;
    consumer.pressed_buttons[0] = fuchsia_input_report::ConsumerControlButton::VOLUME_UP;
    consumer.pressed_buttons[1] = fuchsia_input_report::ConsumerControlButton::VOLUME_DOWN;
    consumer.pressed_buttons[2] = fuchsia_input_report::ConsumerControlButton::PAUSE;
    consumer.pressed_buttons[3] = fuchsia_input_report::ConsumerControlButton::MIC_MUTE;
    consumer.pressed_buttons[4] = fuchsia_input_report::ConsumerControlButton::REBOOT;

    hid_input_report::InputReport report;
    report.report = consumer;

    fake_device_->SetReport(report);
  }
  RunLoopUntilIdle();

  // Check the report.
  {
    ASSERT_EQ(report_count_, 1);
    ASSERT_TRUE(last_report_.media_buttons->volume_up);
    ASSERT_TRUE(last_report_.media_buttons->volume_down);
    ASSERT_TRUE(last_report_.media_buttons->mic_mute);
    ASSERT_TRUE(last_report_.media_buttons->reset);
    ASSERT_TRUE(last_report_.media_buttons->pause);
  }
}

}  // namespace

}  // namespace ui_input
