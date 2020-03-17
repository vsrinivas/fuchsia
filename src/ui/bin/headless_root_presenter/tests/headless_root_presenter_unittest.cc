// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <fuchsia/ui/activity/cpp/fidl_test_base.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/ui/bin/headless_root_presenter/app.h"

namespace headless_root_presenter {
namespace testing {
class MockMediaButtonsListener : public fuchsia::ui::policy::MediaButtonsListener {
 public:
  void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
    last_event_ = std::make_unique<fuchsia::ui::input::MediaButtonsEvent>(std::move(event));
    media_button_event_count_++;
  }
  int GetMediaButtonEventCount() { return media_button_event_count_; }
  fuchsia::ui::input::MediaButtonsEvent* GetLastEvent() { return last_event_.get(); }

 private:
  uint32_t media_button_event_count_ = 0;
  std::unique_ptr<fuchsia::ui::input::MediaButtonsEvent> last_event_;
};

class FakeActivityTracker : public fuchsia::ui::activity::testing::Tracker_TestBase {
 public:
  void NotImplemented_(const std::string& name) final { ZX_DEBUG_ASSERT_IMPLEMENTED; }

  fidl::InterfaceRequestHandler<fuchsia::ui::activity::Tracker> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  void ReportDiscreteActivity(fuchsia::ui::activity::DiscreteActivity activity,
                              zx_time_t event_time,
                              ReportDiscreteActivityCallback callback) override {
    activities_.push_back(std::move(activity));
    callback();
  }

  const std::vector<fuchsia::ui::activity::DiscreteActivity>& activities() const {
    return activities_;
  }

 private:
  std::vector<fuchsia::ui::activity::DiscreteActivity> activities_;
  fidl::Binding<fuchsia::ui::activity::Tracker> binding_{this};
};

class AppUnitTest : public gtest::TestLoopFixture {
 public:
  AppUnitTest() : listener_binding_(&listener_) {
    context_ = context_provider_.context();
    context_provider_.service_directory_provider()->AddService(
        fake_tracker_.GetHandler(dispatcher()));

    app_ = std::make_unique<headless_root_presenter::App>(command_line_, &loop_,
                                                          context_provider_.TakeContext());
    SetupMockDevice();
  }
  void SetUp() override { TestLoopFixture::SetUp(); }

  void SetupMockDevice() {
    fuchsia::ui::input::DeviceDescriptor device_descriptor;
    device_descriptor.media_buttons =
        std::make_unique<fuchsia::ui::input::MediaButtonsDescriptor>();

    fuchsia::ui::input::InputDeviceRegistryPtr inpReg;
    context_provider_.ConnectToPublicService(inpReg.NewRequest());
    inpReg->RegisterDevice(std::move(device_descriptor), input_device_.NewRequest());
  }

  void RegisterMockListener() {
    fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener_handle;
    listener_binding_.Bind(listener_handle.NewRequest());

    fuchsia::ui::policy::DeviceListenerRegistryPtr dReg;
    context_provider_.ConnectToPublicService(dReg.NewRequest());
    dReg->RegisterMediaButtonsListener(std::move(listener_handle));
  }

  sys::testing::ComponentContextProvider context_provider_;
  sys::ComponentContext* context_;

  fuchsia::ui::input::InputDevicePtr input_device_;
  MockMediaButtonsListener listener_;
  fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> listener_binding_;

  const int argc_ = 1;
  const char* argv_[1]{"headless_root_presenter"};
  fxl::CommandLine command_line_{fxl::CommandLineFromArgcArgv(argc_, argv_)};
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<headless_root_presenter::App> app_;

  FakeActivityTracker fake_tracker_;
};

fuchsia::ui::input::InputReport CreateOneReport() {
  std::unique_ptr<fuchsia::ui::input::MediaButtonsReport> media_buttons_report =
      std::make_unique<fuchsia::ui::input::MediaButtonsReport>();
  media_buttons_report->volume_down = true;
  fuchsia::ui::input::InputReport input_report;
  input_report.media_buttons = std::move(media_buttons_report);
  return input_report;
}

// TODO(48425) - Tests are DISABLED because they are flaking.
TEST_F(AppUnitTest, DISABLED_NormalFlowTest) {
  RegisterMockListener();
  RunLoopUntilIdle();
  int current_count = listener_.GetMediaButtonEventCount();

  //- inject media report via device
  input_device_->DispatchReport(CreateOneReport());
  RunLoopUntilIdle();

  //- capture listener wakeup
  EXPECT_TRUE(listener_.GetMediaButtonEventCount() == current_count + 1);
  EXPECT_TRUE(listener_.GetLastEvent()->volume() == -1);
}

// TODO(48425) - Tests are DISABLED because they are flaking.
TEST_F(AppUnitTest, DISABLED_NoListenerTest) {
  input_device_->DispatchReport(CreateOneReport());
  RunLoopUntilIdle();

  // listener should not wake up and test should not crash
  EXPECT_TRUE(listener_.GetMediaButtonEventCount() == 0);
}

// TODO(48425) - Tests are DISABLED because they are flaking.
TEST_F(AppUnitTest, DISABLED_DisconnectTest) {
  RegisterMockListener();
  RunLoopUntilIdle();
  int current_count = listener_.GetMediaButtonEventCount();

  // disconnect
  input_device_.Unbind();
  RunLoopUntilIdle();

  //- inject media report via device
  input_device_->DispatchReport(CreateOneReport());
  RunLoopUntilIdle();

  //- expect listener not wakeup, and test not crash
  EXPECT_TRUE(listener_.GetMediaButtonEventCount() == current_count);
}
}  // namespace testing
}  // namespace headless_root_presenter
