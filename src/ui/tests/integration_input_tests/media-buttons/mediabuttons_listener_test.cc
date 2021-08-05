// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {
using fuchsia::ui::input::MediaButtonsEvent;

// Common services for each test.
const std::map<std::string, std::string> LocalServices() {
  return {
      // Root Presenter protocols.
      {"fuchsia.ui.input.InputDeviceRegistry",
       "fuchsia-pkg://fuchsia.com/mediabuttons-integration-tests#meta/root_presenter.cmx"},
      {"fuchsia.ui.policy.DeviceListenerRegistry",
       "fuchsia-pkg://fuchsia.com/mediabuttons-integration-tests#meta/root_presenter.cmx"},
      // Scenic protocols.
      {"fuchsia.ui.scenic.Scenic",
       "fuchsia-pkg://fuchsia.com/mediabuttons-integration-tests#meta/scenic.cmx"},
      // Misc protocols.
      {"fuchsia.hardware.display.Provider",
       "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"},
  };
}

// Allow these global services from outside the test environment.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.sysmem.Allocator", "fuchsia.vulkan.loader.Loader",
          "fuchsia.tracing.provider.Registry", "fuchsia.logger.LogSink"};
}

// This implements the MediaButtonsListener class. Its purpose is to test that MediaButton Events
// are actually sent out to the Listeners.
class ButtonsListenerImpl : public fuchsia::ui::policy::MediaButtonsListener {
 public:
  ButtonsListenerImpl(
      fidl::InterfaceRequest<fuchsia::ui::policy::MediaButtonsListener> listener_request,
      fit::function<void(const MediaButtonsEvent&)> on_terminate)
      : listener_binding_(this, std::move(listener_request)),
        on_terminate_(std::move(on_terminate)) {}

 private:
  // |MediaButtonsListener|
  void OnMediaButtonsEvent(fuchsia::ui::input::MediaButtonsEvent event) override {
    on_terminate_(event);
  }
  // |MediaButtonsListener|
  void OnEvent(fuchsia::ui::input::MediaButtonsEvent event, OnEventCallback callback) override {
    on_terminate_(event);
    callback();
  }
  fidl::Binding<fuchsia::ui::policy::MediaButtonsListener> listener_binding_;
  fit::function<void(const MediaButtonsEvent&)> on_terminate_;
};

class MediaButtonsListenerTestWithEnvironment : public gtest::TestWithEnvironmentFixture {
 protected:
  explicit MediaButtonsListenerTestWithEnvironment() {
    auto services = sys::testing::EnvironmentServices::Create(real_env());

    // Add common services.
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    // Enable services from outside this test.
    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }

    test_env_ = CreateNewEnclosingEnvironment("media_buttons_test_env", std::move(services));

    WaitForEnclosingEnvToStart(test_env_.get());
    FX_VLOGS(1) << "Created test environment.";
  }

  ~MediaButtonsListenerTestWithEnvironment() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  sys::testing::EnclosingEnvironment* test_env() { return test_env_.get(); }

  void InjectInput(fuchsia::ui::input::MediaButtonsReport media_buttons_report) {
    fuchsia::ui::input::DeviceDescriptor device;
    device.media_buttons = std::make_unique<fuchsia::ui::input::MediaButtonsDescriptor>();
    auto registry = test_env()->ConnectToService<fuchsia::ui::input::InputDeviceRegistry>();
    fuchsia::ui::input::InputDevicePtr connection;
    registry->RegisterDevice(std::move(device), connection.NewRequest());
    fuchsia::ui::input::InputReport input_report;
    input_report.media_buttons =
        std::make_unique<fuchsia::ui::input::MediaButtonsReport>(std::move(media_buttons_report));
    connection->DispatchReport(std::move(input_report));
    injection_count_++;
  }

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;
  uint32_t injection_count_ = 0;
};

TEST_F(MediaButtonsListenerTestWithEnvironment, MediaButtons) {
  std::optional<MediaButtonsEvent> observed_event;
  fit::function<void(const MediaButtonsEvent&)> on_terminate =
      [&observed_event](const MediaButtonsEvent& observed) {
        observed_event = fidl::Clone(observed);
      };

  // Register the MediaButtons listener against Root Presenter.
  fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener_handle;
  auto button_listener_impl =
      std::make_unique<ButtonsListenerImpl>(listener_handle.NewRequest(), std::move(on_terminate));
  auto root_presenter = test_env()->ConnectToService<fuchsia::ui::policy::DeviceListenerRegistry>();
  root_presenter.set_error_handler([](zx_status_t status) {
    FX_LOGS(FATAL) << "Lost connection to RootPresenter: " << zx_status_get_string(status);
  });
  root_presenter->RegisterMediaButtonsListener(std::move(listener_handle));

  // Post input injection in the future, "long enough" that the RegisterMediaButtonsListener will
  // have succeeded.
  // TODO(fxbug.dev/41384): Make this more reliable by parking a callback on a response for
  // RegisterMediaButtonsListener.
  async::PostDelayedTask(
      dispatcher(),
      [this] {
        auto report = fuchsia::ui::input::MediaButtonsReport{.volume_up = true,
                                                             .volume_down = true,
                                                             .mic_mute = true,
                                                             .reset = false,
                                                             .pause = true,
                                                             .camera_disable = false};
        InjectInput(std::move(report));
      },
      zx::sec(1));

  RunLoopUntil([&observed_event] { return observed_event.has_value(); });

  ASSERT_TRUE(observed_event->has_volume());
  EXPECT_EQ(observed_event->volume(), 0);
  ASSERT_TRUE(observed_event->has_mic_mute());
  EXPECT_TRUE(observed_event->mic_mute());
  ASSERT_TRUE(observed_event->has_pause());
  ASSERT_TRUE(observed_event->pause());
  ASSERT_TRUE(observed_event->has_camera_disable());
  EXPECT_FALSE(observed_event->camera_disable());
}

TEST_F(MediaButtonsListenerTestWithEnvironment, MediaButtonsWithCallback) {
  std::optional<MediaButtonsEvent> observed_event;
  fit::function<void(const MediaButtonsEvent&)> on_terminate =
      [&observed_event](const MediaButtonsEvent& observed) {
        observed_event = fidl::Clone(observed);
      };

  // Register the MediaButtons listener against Root Presenter.
  fidl::InterfaceHandle<fuchsia::ui::policy::MediaButtonsListener> listener_handle;
  auto button_listener_impl =
      std::make_unique<ButtonsListenerImpl>(listener_handle.NewRequest(), std::move(on_terminate));

  auto root_presenter = test_env()->ConnectToService<fuchsia::ui::policy::DeviceListenerRegistry>();
  root_presenter.set_error_handler([](zx_status_t status) {
    FX_LOGS(FATAL) << "Lost connection to RootPresenter: " << zx_status_get_string(status);
  });

  bool listener_registered = false;
  root_presenter->RegisterListener(std::move(listener_handle),
                                   [&listener_registered] { listener_registered = true; });

  RunLoopUntil([&listener_registered] { return listener_registered; });

  auto report = fuchsia::ui::input::MediaButtonsReport{.volume_up = true,
                                                       .volume_down = true,
                                                       .mic_mute = true,
                                                       .reset = false,
                                                       .pause = true,
                                                       .camera_disable = false};
  InjectInput(std::move(report));

  RunLoopUntil([&observed_event] { return observed_event.has_value(); });

  ASSERT_TRUE(observed_event->has_volume());
  EXPECT_EQ(observed_event->volume(), 0);
  ASSERT_TRUE(observed_event->has_mic_mute());
  EXPECT_TRUE(observed_event->mic_mute());
  ASSERT_TRUE(observed_event->has_pause());
  ASSERT_TRUE(observed_event->pause());
  ASSERT_TRUE(observed_event->has_camera_disable());
  EXPECT_FALSE(observed_event->camera_disable());
}
}  // namespace
