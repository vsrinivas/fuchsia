// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/media_buttons_handler.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <cstdlib>

#include "gtest/gtest.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/lib/ui/input/input_device_impl.h"
#include "src/ui/testing/mock_input_device.h"

namespace root_presenter {
namespace {

// A mock for capturing events passed to the listener.
class MockListener : public fuchsia::ui::policy::MediaButtonsListener {
 public:
  MockListener(fidl::InterfaceRequest<fuchsia::ui::policy::MediaButtonsListener> listener_request)
      : binding_(this, std::move(listener_request)) {}

  void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
    if (!last_event_) {
      last_event_ = std::make_unique<fuchsia::ui::input::MediaButtonsEvent>();
    }
    event.Clone(last_event_.get());
    media_button_event_count_++;
  }

  fuchsia::ui::input::MediaButtonsEvent* GetLastEvent() { return last_event_.get(); }
  uint32_t GetMediaButtonEventCount() { return media_button_event_count_; }

 private:
  fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> binding_;
  uint32_t media_button_event_count_ = 0;
  std::unique_ptr<fuchsia::ui::input::MediaButtonsEvent> last_event_;
};

class MockActivityNotifier : public ActivityNotifier {
 public:
  void ReceiveInputEvent(const fuchsia::ui::input::InputEvent& event) override {
    FXL_CHECK(false) << "unimplemented.";
  }
  void ReceiveMediaButtonsEvent(const fuchsia::ui::input::MediaButtonsEvent& event) override {
    if (!last_event_) {
      last_event_ = std::make_unique<fuchsia::ui::input::MediaButtonsEvent>();
    }
    event.Clone(last_event_.get());
    media_button_event_count_++;
  }

  fuchsia::ui::input::MediaButtonsEvent* GetLastEvent() { return last_event_.get(); }
  uint32_t GetMediaButtonEventCount() { return media_button_event_count_; }

 private:
  uint32_t media_button_event_count_ = 0;
  std::unique_ptr<fuchsia::ui::input::MediaButtonsEvent> last_event_;
};

class MediaButtonsHandlerTest : public gtest::TestLoopFixture,
                                public ui_input::InputDeviceImpl::Listener {
 public:
  void SetUp() final {
    handler = std::make_unique<MediaButtonsHandler>(&activity_notifier);

    fuchsia::ui::input::DeviceDescriptor device_descriptor;
    device_descriptor.media_buttons =
        std::make_unique<fuchsia::ui::input::MediaButtonsDescriptor>();

    int id = rand();
    device = std::make_unique<ui_input::InputDeviceImpl>(id, std::move(device_descriptor),
                                                         input_device.NewRequest(), this);
    device_added = false;
  }

  void OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device){};
  void OnReport(ui_input::InputDeviceImpl* input_device, fuchsia::ui::input::InputReport report) {
    handler->OnReport(input_device->id(), std::move(report));
  };

 protected:
  std::unique_ptr<MockListener> CreateListener() {
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener_handle;
    auto mock_listener = std::make_unique<MockListener>(listener_handle.NewRequest());
    handler->RegisterListener(std::move(listener_handle));
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
  MockActivityNotifier activity_notifier;
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

// This test exercises delivering a report to handler after registration.
TEST_F(MediaButtonsHandlerTest, ReportAfterRegistration) {
  auto listener = CreateListener();

  fuchsia::ui::input::MediaButtonsReport media_buttons;
  media_buttons.volume_down = true;

  DispatchReport(std::move(media_buttons));

  EXPECT_TRUE(listener->GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(listener->GetLastEvent()->volume() == -1);

  EXPECT_TRUE(activity_notifier.GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(activity_notifier.GetLastEvent()->volume() == -1);
}

// This test exercises delivering a report to handler before registration. Upon
// registration, the last report should be delivered to the handler.
TEST_F(MediaButtonsHandlerTest, ReportBeforeRegistration) {
  {
    fuchsia::ui::input::MediaButtonsReport media_buttons;
    media_buttons.mic_mute = false;

    DispatchReport(std::move(media_buttons));
  }

  {
    fuchsia::ui::input::MediaButtonsReport media_buttons;
    media_buttons.mic_mute = true;

    DispatchReport(std::move(media_buttons));
  }

  auto listener = CreateListener();

  EXPECT_TRUE(listener->GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(listener->GetLastEvent()->mic_mute());

  // |activity_notifier| receives all inputs before registration
  EXPECT_TRUE(activity_notifier.GetMediaButtonEventCount() == 2);
  EXPECT_TRUE(activity_notifier.GetLastEvent()->mic_mute());
}

// This test ensures multiple listeners receive messages when dispatched by an
// input device.
TEST_F(MediaButtonsHandlerTest, MultipleListeners) {
  auto listener = CreateListener();
  auto listener2 = CreateListener();

  fuchsia::ui::input::MediaButtonsReport media_buttons;
  media_buttons.volume_up = true;

  DispatchReport(std::move(media_buttons));

  EXPECT_TRUE(listener->GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(listener->GetLastEvent()->volume() == 1);

  EXPECT_TRUE(listener2->GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(listener2->GetLastEvent()->volume() == 1);

  EXPECT_TRUE(activity_notifier.GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(activity_notifier.GetLastEvent()->volume() == 1);
}

// This test checks that pause is wired up correctly.
TEST_F(MediaButtonsHandlerTest, PauseButton) {
  auto listener = CreateListener();

  fuchsia::ui::input::MediaButtonsReport media_buttons = {};
  media_buttons.pause = true;

  DispatchReport(media_buttons);

  EXPECT_TRUE(listener->GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(listener->GetLastEvent()->pause());

  EXPECT_TRUE(activity_notifier.GetMediaButtonEventCount() == 1);
  EXPECT_TRUE(activity_notifier.GetLastEvent()->pause());

  media_buttons.pause = false;
  DispatchReport(media_buttons);

  EXPECT_TRUE(listener->GetMediaButtonEventCount() == 2);
  EXPECT_FALSE(listener->GetLastEvent()->pause());

  EXPECT_TRUE(activity_notifier.GetMediaButtonEventCount() == 2);
  EXPECT_FALSE(activity_notifier.GetLastEvent()->pause());
}

}  // namespace
}  // namespace root_presenter
