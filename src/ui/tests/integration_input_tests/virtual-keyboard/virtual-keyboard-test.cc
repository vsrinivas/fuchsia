// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <iostream>
#include <type_traits>

#include <gtest/gtest.h>
#include <test/virtualkeyboard/cpp/fidl.h>

// This test exercises the virtual keyboard visibility interactions between Chromium and Root
// Presenter. It is a multi-component test, and carefully avoids sleeping or polling for component
// coordination.
// - It runs real Root Presenter and Scenic components.
// - It uses a fake display controller; the physical device is unused.
//
// Components involved
// - This test program
// - Root Presenter
// - Scenic
// - WebEngine (built from Chromium)
//
// Setup sequence
// - The test sets up a view hierarchy with two views:
//   - Top level scene, owned by Root Presenter.
//   - Bottom view, owned by Chromium.

namespace {

using test::virtualkeyboard::InputPositionListener;
using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Common services for each test.
std::map<std::string, std::string> LocalServices() {
  return {
      // Root presenter protocols.
      {"fuchsia.input.virtualkeyboard.Manager",
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/root_presenter.cmx"},
      {"fuchsia.input.virtualkeyboard.ControllerCreator",
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/root_presenter.cmx"},
      {"fuchsia.ui.input.InputDeviceRegistry",
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/root_presenter.cmx"},
      {"fuchsia.ui.policy.Presenter",
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/root_presenter.cmx"},
      {fuchsia::ui::accessibility::view::Registry::Name_,
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/root_presenter.cmx"},
      // Scenic protocols.
      {"fuchsia.ui.scenic.Scenic",
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/scenic.cmx"},
      {"fuchsia.ui.pointerinjector.Registry",
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/scenic.cmx"},
      {"fuchsia.ui.focus.FocusChainListenerRegistry",
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/scenic.cmx"},
      // IME protocols.
      {fuchsia::ui::input::ImeService::Name_,
       "fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx"},
      {fuchsia::ui::input::ImeVisibilityService::Name_,
       "fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx"},
      {fuchsia::ui::input3::Keyboard::Name_,
       "fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx"},
      // Netstack protocols.
      {fuchsia::netstack::Netstack::Name_,
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/netstack.cmx"},
      {fuchsia::net::interfaces::State::Name_,
       "fuchsia-pkg://fuchsia.com/virtual-keyboard-test#meta/netstack.cmx"},
      // Misc protocols.
      {"fuchsia.cobalt.LoggerFactory",
       "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx"},
      {"fuchsia.hardware.display.Provider",
       "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"},
      {fuchsia::fonts::Provider::Name_, "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx"},
      {fuchsia::intl::PropertyProvider::Name_,
       "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cmx"},
      {fuchsia::memorypressure::Provider::Name_,
       "fuchsia-pkg://fuchsia.com/memory_monitor#meta/memory_monitor.cmx"},
      {fuchsia::accessibility::semantics::SemanticsManager::Name_,
       "fuchsia-pkg://fuchsia.com/a11y-manager#meta/a11y-manager.cmx"},
      {fuchsia::web::ContextProvider::Name_,
       "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx"},
  };
}

// Allow these global services from outside the test environment.
std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator",
          "fuchsia.scheduler.ProfileProvider"};
}

class WebEngineTest : public gtest::TestWithEnvironmentFixture, public InputPositionListener {
 protected:
  explicit WebEngineTest() {
    auto services = TestWithEnvironment::CreateServices();

    // Key part of service setup: have this test component vend the |InputPositionListener| service
    // in the constructed environment.
    {
      const zx_status_t is_ok =
          services->AddService<InputPositionListener>(input_position_listener_.GetHandler(this));
      FX_CHECK(is_ok == ZX_OK);
    }

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

    test_env_ = CreateNewEnclosingEnvironment("touch_input_test_env", std::move(services));

    WaitForEnclosingEnvToStart(test_env_.get());

    FX_LOGS(INFO) << "Created test environment.";

    // Get the display dimensions
    auto scenic = test_env_->ConnectToService<fuchsia::ui::scenic::Scenic>();
    scenic->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
      if (display_info.width_in_px > 0 && display_info.height_in_px > 0) {
        display_width_ = display_info.width_in_px;
        display_height_ = display_info.height_in_px;
        FX_LOGS(INFO) << "Got display_width = " << *display_width_
                      << " and display_height = " << *display_height_;
      }
    });
    RunLoopUntil([this] { return display_width_.has_value() && display_height_.has_value(); });

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);
  }

  void LaunchChromium() {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    // Instruct Root Presenter to present test's View.
    auto root_presenter = test_env_->ConnectToService<fuchsia::ui::policy::Presenter>();
    root_presenter->PresentOrReplaceView(std::move(view_holder_token),
                                         /* presentation */ nullptr);

    // Start client app inside the test environment.
    // Note well. We launch the client component directly, and ask for its ViewProvider service
    // directly, to closely model production setup.
    fuchsia::sys::LaunchInfo launch_info{
        .url =
            "fuchsia-pkg://fuchsia.com/web-virtual-keyboard-client#meta/"
            "web-virtual-keyboard-client.cmx"};
    // Create a point-to-point offer-use connection between parent and child.
    child_services_ = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
    client_component_ = test_env_->CreateComponent(std::move(launch_info));

    auto view_provider = child_services_->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView(std::move(view_token.value), /* in */ nullptr,
                              /* out */ nullptr);
  }

  // Injects an input event, and posts a task to retry after `kTapRetryInterval`.
  //
  // We post the retry task because the first input event we send to WebEngine may be lost.
  // There is no guarantee that, just because the web app has returned the location of the
  // input box, that Chromium is actually ready to receive events from Scenic.
  void TryInject(int32_t x, int32_t y) {
    InjectInput(x, y);
    inject_retry_task_.emplace(
        [this, x, y](auto dispatcher, auto task, auto status) { TryInject(x, y); });
    FX_CHECK(inject_retry_task_->PostDelayed(dispatcher(), kTapRetryInterval) == ZX_OK);
  };

  void CancelInject() { inject_retry_task_.reset(); }

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return *display_width_; }
  uint32_t display_height() const { return *display_height_; }
  const std::optional<test::virtualkeyboard::BoundingBox>& input_position() const {
    return input_position_;
  }

  sys::testing::EnclosingEnvironment* test_env() { return test_env_.get(); }
  fuchsia::sys::ComponentControllerPtr& client_component() { return client_component_; }
  sys::ServiceDirectory& child_services() { return *child_services_; }

 private:
  // The typical latency on devices we've tested is ~60 msec. The retry interval is chosen to be
  // a) Long enough that it's unlikely that we send a new tap while a previous tap is still being
  //    processed. That is, it should be far more likely that a new tap is sent because the first
  //    tap was lost, than because the system is just running slowly.
  // b) Short enough that we don't slow down tryjobs.
  static constexpr auto kTapRetryInterval = zx::sec(1);

  // |test::virtualkeyboard::InputPositionListener|
  void Notify(test::virtualkeyboard::BoundingBox bounding_box) override {
    input_position_ = bounding_box;
  }

  // Inject directly into Root Presenter, using fuchsia.ui.input FIDLs.
  void InjectInput(int32_t x, int32_t y) {
    using fuchsia::ui::input::InputReport;
    // Device parameters
    auto parameters = fuchsia::ui::input::TouchscreenDescriptor::New();
    *parameters = {.x = {.range = {.min = 0, .max = static_cast<int32_t>(display_width())}},
                   .y = {.range = {.min = 0, .max = static_cast<int32_t>(display_height())}},
                   .max_finger_id = 10};
    FX_LOGS(INFO) << "Registering touchscreen with x touch range = (" << parameters->x.range.min
                  << ", " << parameters->x.range.max << ") "
                  << "and y touch range = (" << parameters->y.range.min << ", "
                  << parameters->y.range.max << ").";

    // Register it against Root Presenter.
    fuchsia::ui::input::DeviceDescriptor device{.touchscreen = std::move(parameters)};
    auto registry = test_env_->ConnectToService<fuchsia::ui::input::InputDeviceRegistry>();
    fuchsia::ui::input::InputDevicePtr connection;
    registry->RegisterDevice(std::move(device), connection.NewRequest());

    {
      // Inject input report.
      auto touch = fuchsia::ui::input::TouchscreenReport::New();
      *touch = {.touches = {{.finger_id = 1, .x = x, .y = y}}};
      InputReport report{.event_time = static_cast<uint64_t>(zx::clock::get_monotonic().get()),
                         .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
      FX_LOGS(INFO) << "Dispatching touch report at (" << x << "," << y << ")";
    }

    {
      // Inject conclusion (empty) report.
      auto touch = fuchsia::ui::input::TouchscreenReport::New();
      InputReport report{.event_time = static_cast<uint64_t>(zx::clock::get_monotonic().get()),
                         .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
    }

    FX_LOGS(INFO) << "*** Tap injected";
  }

  fidl::BindingSet<test::virtualkeyboard::InputPositionListener> input_position_listener_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;

  std::optional<uint32_t> display_width_;
  std::optional<uint32_t> display_height_;
  std::optional<test::virtualkeyboard::BoundingBox> input_position_;

  fuchsia::sys::ComponentControllerPtr client_component_;
  std::shared_ptr<sys::ServiceDirectory> child_services_;

  std::optional<async::Task> inject_retry_task_;
};

TEST_F(WebEngineTest, ShowAndHideKeyboard) {
  LaunchChromium();
  client_component().events().OnTerminated = [](int64_t return_code,
                                                fuchsia::sys::TerminationReason reason) {
    if (return_code != 0) {
      FX_LOGS(FATAL) << "Web appterminated abnormally with return_code=" << return_code
                     << ", reason="
                     << static_cast<std::underlying_type_t<decltype(reason)>>(reason);
    }
  };

  FX_LOGS(INFO) << "Getting initial keyboard state";
  std::optional<bool> is_keyboard_visible;
  auto virtualkeyboard_manager =
      test_env()->ConnectToService<fuchsia::input::virtualkeyboard::Manager>();
  virtualkeyboard_manager->WatchTypeAndVisibility(
      [&is_keyboard_visible](auto text_type, auto is_visible) {
        is_keyboard_visible = is_visible;
      });
  RunLoopUntil([&]() { return is_keyboard_visible.has_value(); });
  ASSERT_FALSE(is_keyboard_visible.value());
  is_keyboard_visible.reset();

  FX_LOGS(INFO) << "Getting input box position";
  RunLoopUntil([this]() { return input_position().has_value(); });

  FX_LOGS(INFO) << "Tapping _inside_ input box";
  auto input_pos = *input_position();
  int32_t input_center_x = (input_pos.x0 + input_pos.x1) / 2;
  int32_t input_center_y = (input_pos.y0 + input_pos.y1) / 2;
  TryInject(input_center_x, input_center_y);

  FX_LOGS(INFO) << "Waiting for keyboard to be visible";
  virtualkeyboard_manager->WatchTypeAndVisibility(
      [&is_keyboard_visible](auto text_type, auto is_visible) {
        is_keyboard_visible = is_visible;
      });
  RunLoopUntil([&]() { return is_keyboard_visible.has_value(); });
  ASSERT_TRUE(is_keyboard_visible.value());
  CancelInject();
  is_keyboard_visible.reset();

  FX_LOGS(INFO) << "Tapping _outside_ input box";
  TryInject(input_pos.x1 + 1, input_pos.y1 + 1);

  FX_LOGS(INFO) << "Waiting for keyboard to be hidden";
  virtualkeyboard_manager->WatchTypeAndVisibility(
      [&is_keyboard_visible](auto text_type, auto is_visible) {
        is_keyboard_visible = is_visible;
      });
  RunLoopUntil([&]() { return is_keyboard_visible.has_value(); });
  ASSERT_FALSE(is_keyboard_visible.value());
  CancelInject();
}

}  // namespace
