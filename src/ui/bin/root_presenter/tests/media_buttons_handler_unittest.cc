// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/media_buttons_handler.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <cstdlib>

#include <gtest/gtest.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/testing/mock_input_device.h"

namespace root_presenter {
namespace {

// A mock implementation which expects events via `OnMediaButtonsEvent()`.
class LegacyListener : public fuchsia::ui::policy::MediaButtonsListener {
 public:
  explicit LegacyListener(MediaButtonsHandler* handler) : binding_(this) {
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> fidl_handle;
    binding_.Bind(fidl_handle.NewRequest());
    handler->RegisterListener(std::move(fidl_handle));
  }

  // fuchsia::ui::policy::MediaButtonsListener
  void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
    if (!last_event_) {
      last_event_ = std::make_unique<fuchsia::ui::input::MediaButtonsEvent>();
    }
    event.Clone(last_event_.get());
    event_count_++;
  }

  // fuchsia::ui::policy::MediaButtonsListener
  void OnEvent(fuchsia::ui::input::MediaButtonsEvent event,
               fuchsia::ui::policy::MediaButtonsListener::OnEventCallback cb) override {
    FAIL() << "Should not receive modern events on legacy listener.";
  }

  // Test support.
  fuchsia::ui::input::MediaButtonsEvent* GetLastEvent() { return last_event_.get(); }
  size_t GetEventCount() const { return event_count_; }

 private:
  fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> binding_;
  size_t event_count_ = 0;
  std::unique_ptr<fuchsia::ui::input::MediaButtonsEvent> last_event_;
};

// A mock implementation which expects events via `OnEvent()`.
class ModernListener : public fuchsia::ui::policy::MediaButtonsListener {
 public:
  explicit ModernListener(MediaButtonsHandler* handler) : binding_(this) {
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> fidl_handle;
    binding_.Bind(fidl_handle.NewRequest());
    handler->RegisterListener2(std::move(fidl_handle));
  }

  // fuchsia::ui::policy::MediaButtonsListener
  void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
    FAIL() << "Should not receive legacy events on modern listener.";
  }

  // fuchsia::ui::policy::MediaButtonsListener
  void OnEvent(fuchsia::ui::input::MediaButtonsEvent event,
               fuchsia::ui::policy::MediaButtonsListener::OnEventCallback cb) override {
    if (!last_event_) {
      last_event_ = std::make_unique<fuchsia::ui::input::MediaButtonsEvent>();
    }
    event.Clone(last_event_.get());
    event_count_++;
    cb();
  }

  // Test support.
  fuchsia::ui::input::MediaButtonsEvent* GetLastEvent() { return last_event_.get(); }
  size_t GetEventCount() const { return event_count_; }

 private:
  fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> binding_;
  size_t event_count_ = 0;
  std::unique_ptr<fuchsia::ui::input::MediaButtonsEvent> last_event_;
};

template <typename ListenerT>
class MediaButtonsHandlerTest : public gtest::TestLoopFixture,
                                public ui_input::InputDeviceImpl::Listener {
 public:
  void SetUp() final {
    handler = std::make_unique<MediaButtonsHandler>();

    fuchsia::ui::input::DeviceDescriptor device_descriptor;
    device_descriptor.media_buttons =
        std::make_unique<fuchsia::ui::input::MediaButtonsDescriptor>();

    int id = rand();
    device = std::make_unique<ui_input::InputDeviceImpl>(id, std::move(device_descriptor),
                                                         input_device.NewRequest(), this);
    device_added = false;
  }

  void OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) {}
  void OnReport(ui_input::InputDeviceImpl* input_device, fuchsia::ui::input::InputReport report) {
    handler->OnReport(input_device->id(), std::move(report));
  }

 protected:
  std::unique_ptr<ListenerT> CreateListener() {
    auto mock_listener = std::make_unique<ListenerT>(handler.get());
    RunLoopUntilIdle();
    return mock_listener;
  }

  void DispatchReport(fuchsia::ui::input::MediaButtonsReport report) {
    AddDevice();

    std::unique_ptr<fuchsia::ui::input::MediaButtonsReport> media_buttons_report =
        std::make_unique<fuchsia::ui::input::MediaButtonsReport>();
    fidl::Clone(report, media_buttons_report.get());

    fuchsia::ui::input::InputReport input_report;
    input_report.media_buttons = std::move(media_buttons_report);

    input_device->DispatchReport(std::move(input_report));
    RunLoopUntilIdle();
  }

  fuchsia::ui::input::InputDevicePtr input_device;
  std::unique_ptr<ui_input::InputDeviceImpl> device;

  std::unique_ptr<MediaButtonsHandler> handler;
  bool device_added;

 private:
  void AddDevice() {
    if (device_added) {
      return;
    }

    handler->OnDeviceAdded(device.get());
    device_added = true;
  }
};

using ListenerTypes = ::testing::Types<LegacyListener, ModernListener>;
TYPED_TEST_SUITE(MediaButtonsHandlerTest, ListenerTypes);

// This test exercises delivering a report to handler after registration.
TYPED_TEST(MediaButtonsHandlerTest, ReportAfterRegistration) {
  auto listener = this->CreateListener();

  fuchsia::ui::input::MediaButtonsReport media_buttons;
  media_buttons.volume_down = true;

  this->DispatchReport(std::move(media_buttons));

  EXPECT_EQ(listener->GetEventCount(), 1U);
  EXPECT_EQ(listener->GetLastEvent()->volume(), -1);
}

// This test exercises delivering a report to handler before registration. Upon
// registration, the last report should be delivered to the handler.
TYPED_TEST(MediaButtonsHandlerTest, ReportBeforeRegistration) {
  {
    fuchsia::ui::input::MediaButtonsReport media_buttons;
    media_buttons.mic_mute = false;

    this->DispatchReport(std::move(media_buttons));
  }

  {
    fuchsia::ui::input::MediaButtonsReport media_buttons;
    media_buttons.mic_mute = true;

    this->DispatchReport(std::move(media_buttons));
  }

  auto listener = this->CreateListener();

  EXPECT_EQ(listener->GetEventCount(), 1U);
  EXPECT_TRUE(listener->GetLastEvent()->mic_mute());
}

// This test ensures multiple listeners receive messages when dispatched by an
// input device.
TYPED_TEST(MediaButtonsHandlerTest, MultipleListeners) {
  auto listener = this->CreateListener();
  auto listener2 = this->CreateListener();

  fuchsia::ui::input::MediaButtonsReport media_buttons;
  media_buttons.volume_up = true;

  this->DispatchReport(std::move(media_buttons));

  EXPECT_EQ(listener->GetEventCount(), 1U);
  EXPECT_EQ(listener->GetLastEvent()->volume(), 1);

  EXPECT_EQ(listener2->GetEventCount(), 1U);
  EXPECT_EQ(listener2->GetLastEvent()->volume(), 1);
}

// This test checks that pause is wired up correctly.
TYPED_TEST(MediaButtonsHandlerTest, PauseButton) {
  auto listener = this->CreateListener();

  fuchsia::ui::input::MediaButtonsReport media_buttons = {};
  media_buttons.pause = true;

  this->DispatchReport(media_buttons);

  EXPECT_EQ(listener->GetEventCount(), 1U);
  EXPECT_TRUE(listener->GetLastEvent()->pause());

  media_buttons.pause = false;
  this->DispatchReport(media_buttons);

  EXPECT_EQ(listener->GetEventCount(), 2U);
  EXPECT_FALSE(listener->GetLastEvent()->pause());
}

// This test ensures that the camera button state is sent forward
// if the mic and camera are tied together.
TYPED_TEST(MediaButtonsHandlerTest, MicCameraTogether) {
  {
    fuchsia::ui::input::MediaButtonsReport media_buttons;
    media_buttons.mic_mute = true;
    media_buttons.camera_disable = true;

    this->DispatchReport(std::move(media_buttons));
  }

  auto listener = this->CreateListener();

  EXPECT_EQ(listener->GetEventCount(), 1U);
  EXPECT_TRUE(listener->GetLastEvent()->mic_mute());
  EXPECT_TRUE(listener->GetLastEvent()->camera_disable());
}

// This test ensures that the camera button state is sent forward
// if the mic and camera are separately controlled.
TYPED_TEST(MediaButtonsHandlerTest, MicCameraSeparate) {
  {
    fuchsia::ui::input::MediaButtonsReport media_buttons;
    media_buttons.mic_mute = true;
    media_buttons.camera_disable = false;

    this->DispatchReport(std::move(media_buttons));
  }

  auto listener = this->CreateListener();

  EXPECT_EQ(listener->GetEventCount(), 1U);
  EXPECT_TRUE(listener->GetLastEvent()->mic_mute());
  EXPECT_FALSE(listener->GetLastEvent()->camera_disable());
}

// This test ensures that the button state is delivered to media button
// listeners when FDR is active.
TYPED_TEST(MediaButtonsHandlerTest, MediaButtonListeningDuringFDR) {
  {
    fuchsia::ui::input::MediaButtonsReport media_buttons;
    media_buttons.reset = true;
    media_buttons.volume_down = true;

    this->DispatchReport(std::move(media_buttons));
  }

  auto listener = this->CreateListener();

  EXPECT_EQ(listener->GetEventCount(), 1U);
  EXPECT_EQ(listener->GetLastEvent()->volume(), -1);
}

}  // namespace
}  // namespace root_presenter
