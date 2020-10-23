// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <set>

#include <ddk/device.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <gtest/gtest.h>

#include "lib/gtest/test_loop_fixture.h"
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

 protected:
  MockInputDeviceRegistry registry_;
  InputReader input_reader_;
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_device_;
  std::optional<fuchsia_input_report::InputDevice::SyncClient> client_;

  void StartDevice() { AddDevice(std::move(token_client_)); }

 private:
  zx::channel token_client_;
  void SetUp() override {
    StartInputReader(&input_reader_);

    // Make the channels and the fake device.
    zx::channel token_server;
    ASSERT_EQ(zx::channel::create(0, &token_server, &token_client_), ZX_OK);

    // Make the fake device's FIDL interface.
    fake_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>(
        fidl::InterfaceRequest<fuchsia::input::report::InputDevice>(std::move(token_server)),
        async_get_default_dispatcher());
  }

  void TearDown() override {}
};

TEST_F(ReaderInterpreterInputTest, TouchScreen) {
  // Add a touchscreen descriptor.
  {
    auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    auto touch = descriptor->mutable_touch()->mutable_input();
    touch->set_touch_type(fuchsia::input::report::TouchType::TOUCHSCREEN);
    touch->set_max_contacts(100);

    fuchsia::input::report::Axis axis;
    axis.unit.type = fuchsia::input::report::UnitType::NONE;
    axis.unit.exponent = 0;
    axis.range.min = 0;
    axis.range.max = 300;

    fuchsia::input::report::ContactInputDescriptor contact;
    contact.set_position_x(axis);

    axis.range.max = 500;
    contact.set_position_y(axis);

    touch->mutable_contacts()->push_back(std::move(contact));

    fake_device_->SetDescriptor(std::move(descriptor));
  }

  // Add the device.
  StartDevice();
  RunLoopUntilIdle();

  // Check the touchscreen descriptor.
  {
    ASSERT_NE(last_device_, nullptr);
    fuchsia::ui::input::DeviceDescriptor* descriptor = last_device_->descriptor();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->touchscreen, nullptr);
    EXPECT_EQ(descriptor->touchscreen->x.range.min, 0);
    EXPECT_EQ(descriptor->touchscreen->x.range.max, 300);
    EXPECT_EQ(descriptor->touchscreen->y.range.min, 0);
    EXPECT_EQ(descriptor->touchscreen->y.range.max, 500);
  }

  // Send a Touchscreen report.
  {
    fuchsia::input::report::InputReport report;
    fuchsia::input::report::ContactInputReport contact;

    contact.set_contact_id(10);
    contact.set_position_x(30);
    contact.set_position_y(50);

    report.mutable_touch()->mutable_contacts()->push_back(std::move(contact));

    std::vector<fuchsia::input::report::InputReport> reports;
    reports.push_back(std::move(report));
    fake_device_->SetReports(std::move(reports));
  }

  RunLoopUntilIdle();

  // Check the touchscreen report.
  {
    ASSERT_EQ(report_count_, 1);
    EXPECT_EQ(last_report_.touchscreen->touches.size(), 1U);
    EXPECT_EQ(last_report_.touchscreen->touches[0].finger_id, 10U);
    EXPECT_EQ(last_report_.touchscreen->touches[0].x, 30);
    EXPECT_EQ(last_report_.touchscreen->touches[0].y, 50);
  }

  // Send another Touchscreen report.
  {
    fuchsia::input::report::InputReport report;
    fuchsia::input::report::ContactInputReport contact;

    contact.set_contact_id(10);
    contact.set_position_x(30);
    contact.set_position_y(50);

    report.mutable_touch()->mutable_contacts()->push_back(std::move(contact));

    std::vector<fuchsia::input::report::InputReport> reports;
    reports.push_back(std::move(report));
    fake_device_->SetReports(std::move(reports));
  }

  RunLoopUntilIdle();

  // Check the second touchscreen report.
  {
    ASSERT_EQ(report_count_, 2);
    EXPECT_EQ(last_report_.touchscreen->touches.size(), 1U);
    EXPECT_EQ(last_report_.touchscreen->touches[0].finger_id, 10U);
    EXPECT_EQ(last_report_.touchscreen->touches[0].x, 30);
    EXPECT_EQ(last_report_.touchscreen->touches[0].y, 50);
  }
}

TEST_F(ReaderInterpreterInputTest, DeviceRemovesCorrectly) {
  // Add a touchscreen descriptor.
  {
    auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    auto touch = descriptor->mutable_touch()->mutable_input();
    touch->set_touch_type(fuchsia::input::report::TouchType::TOUCHSCREEN);
    touch->set_max_contacts(100);

    fuchsia::input::report::Axis axis;
    axis.unit.type = fuchsia::input::report::UnitType::NONE;
    axis.unit.exponent = 0;
    axis.range.min = 0;
    axis.range.max = 300;

    fuchsia::input::report::ContactInputDescriptor contact;
    contact.set_position_x(axis);

    axis.range.max = 500;
    contact.set_position_y(axis);

    touch->mutable_contacts()->push_back(std::move(contact));

    fake_device_->SetDescriptor(std::move(descriptor));
  }

  // Add the device.
  StartDevice();
  RunLoopUntilIdle();

  // Check the touchscreen descriptor.
  {
    ASSERT_NE(last_device_, nullptr);
    fuchsia::ui::input::DeviceDescriptor* descriptor = last_device_->descriptor();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->touchscreen, nullptr);
    EXPECT_EQ(descriptor->touchscreen->x.range.min, 0);
    EXPECT_EQ(descriptor->touchscreen->x.range.max, 300);
    EXPECT_EQ(descriptor->touchscreen->y.range.min, 0);
    EXPECT_EQ(descriptor->touchscreen->y.range.max, 500);
  }

  // Remove the device.
  fake_device_.reset();
  RunLoopUntilIdle();
}

TEST_F(ReaderInterpreterInputTest, ConsumerControl) {
  // Add a descriptor.
  {
    auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    descriptor->mutable_consumer_control()->mutable_input()->set_buttons(
        {fuchsia::input::report::ConsumerControlButton::VOLUME_UP,
         fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN,
         fuchsia::input::report::ConsumerControlButton::PAUSE,
         fuchsia::input::report::ConsumerControlButton::MIC_MUTE,
         fuchsia::input::report::ConsumerControlButton::REBOOT,
         fuchsia::input::report::ConsumerControlButton::CAMERA_DISABLE});

    fake_device_->SetDescriptor(std::move(descriptor));
  }

  // Add the device.
  StartDevice();
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
                  fuchsia::ui::input::kReset | fuchsia::ui::input::kCameraDisable);
  }

  // Send a report.
  {
    fuchsia::input::report::InputReport report;
    report.mutable_consumer_control()->set_pressed_buttons(
        {fuchsia::input::report::ConsumerControlButton::VOLUME_UP,
         fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN,
         fuchsia::input::report::ConsumerControlButton::PAUSE,
         fuchsia::input::report::ConsumerControlButton::MIC_MUTE,
         fuchsia::input::report::ConsumerControlButton::REBOOT,
         fuchsia::input::report::ConsumerControlButton::CAMERA_DISABLE});

    std::vector<fuchsia::input::report::InputReport> reports;
    reports.push_back(std::move(report));
    fake_device_->SetReports(std::move(reports));
  }
  RunLoopUntilIdle();

  // Check the report.
  {
    ASSERT_EQ(report_count_, 1);
    ASSERT_TRUE(last_report_.media_buttons->volume_up);
    ASSERT_TRUE(last_report_.media_buttons->volume_down);
    ASSERT_TRUE(last_report_.media_buttons->mic_mute);
    ASSERT_TRUE(last_report_.media_buttons->camera_disable);
    ASSERT_TRUE(last_report_.media_buttons->reset);
    ASSERT_TRUE(last_report_.media_buttons->pause);
  }
}

TEST_F(ReaderInterpreterInputTest, Mouse) {
  // Add a descriptor.
  {
    auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    auto mouse = descriptor->mutable_mouse()->mutable_input();

    fuchsia::input::report::Axis axis;
    axis.unit.type = fuchsia::input::report::UnitType::NONE;
    axis.unit.exponent = 0;
    axis.range.min = -100;
    axis.range.max = 100;
    mouse->set_movement_x(axis);

    axis.range.min = -200;
    axis.range.max = 200;
    mouse->set_movement_y(axis);

    mouse->set_buttons({1, 3});

    fake_device_->SetDescriptor(std::move(descriptor));
  }

  // Add the device.
  StartDevice();
  RunLoopUntilIdle();

  // Check the descriptor.
  {
    ASSERT_NE(last_device_, nullptr);
    fuchsia::ui::input::DeviceDescriptor* descriptor = last_device_->descriptor();
    ASSERT_NE(descriptor, nullptr);
    ASSERT_NE(descriptor->mouse, nullptr);
    EXPECT_EQ(descriptor->mouse->buttons, 0b101U);
    EXPECT_EQ(descriptor->mouse->rel_x.range.min, -100);
    EXPECT_EQ(descriptor->mouse->rel_x.range.max, 100);
    EXPECT_EQ(descriptor->mouse->rel_y.range.min, -200);
    EXPECT_EQ(descriptor->mouse->rel_y.range.max, 200);
  }

  // Send a report.
  {
    fuchsia::input::report::InputReport report;
    report.mutable_mouse()->set_movement_x(100);
    report.mutable_mouse()->set_movement_y(200);
    report.mutable_mouse()->set_pressed_buttons({1, 3});

    std::vector<fuchsia::input::report::InputReport> reports;
    reports.push_back(std::move(report));
    fake_device_->SetReports(std::move(reports));
  }
  RunLoopUntilIdle();

  // Check the report.
  {
    ASSERT_EQ(report_count_, 1);
    EXPECT_EQ(last_report_.mouse->pressed_buttons, 0b101U);
    EXPECT_EQ(last_report_.mouse->rel_x, 100);
    EXPECT_EQ(last_report_.mouse->rel_y, 200);
  }
}

}  // namespace

}  // namespace ui_input
