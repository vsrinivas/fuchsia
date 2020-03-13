// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/test/ui/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <zircon/types.h>

#include <iostream>

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"

// This test exercises the touch input dispatch path from Root Presenter to a Scenic client. It is a
// multi-component test, and carefully avoids sleeping or polling for component coordination.
// - It runs a real Root Presenter; other top-level programs, like Tiles, interfere with this test.
// - It runs a real Scenic; the display controller MUST be free.
//
// Components involved
// - This test program
// - Root Presenter
// - Scenic
// - Child view, a Scenic client
//
// Touch dispatch path
// - Test program's injection -> Root Presenter -> Scenic -> Child view
//
// Setup sequence
// - The test sets up a view hierarchy with three views:
//   - Top level scene, owned by Root Presenter.
//   - Middle view, owned by this test.
//   - Bottom view, owned by the child view.
// - The test waits for a Scenic event that verifies the child has UI content in the scene graph.
// - The test injects input into Root Presenter, emulating a display's touch report.
// - Root Presenter dispatches the touch event to Scenic, which in turn dispatches it to the child.
// - The child receives the touch event and reports back to the test over a custom test-only FIDL.
// - Test waits for the child to report a touch; when it receives the report, it quits successfully.

namespace {

using fuchsia::test::ui::ResponseListener;
using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Fuchsia components that this test launches.
constexpr char kRootPresenter[] =
    "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx";
constexpr char kScenic[] = "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx";

class TouchInputTest : public sys::testing::TestWithEnvironment, public ResponseListener {
 protected:
  TouchInputTest() : response_listener_(this) {
    auto services = sys::testing::EnvironmentServices::Create(real_env());
    zx_status_t is_ok;

    // Key part of service setup: have this test component vend the |ResponseListener| service in
    // the constructed environment.
    is_ok = services->AddService<ResponseListener>(
        [this](fidl::InterfaceRequest<ResponseListener> request) {
          response_listener_.Bind(std::move(request));
        });
    FXL_CHECK(is_ok == ZX_OK);

    // Set up Scenic inside the test environment.
    {
      fuchsia::sys::LaunchInfo launch;
      launch.url = kScenic;
      if (FXL_VLOG_IS_ON(1)) {
        launch.arguments.emplace();
        launch.arguments->push_back("--verbose=2");
      }
      is_ok = services->AddServiceWithLaunchInfo(std::move(launch), "fuchsia.ui.scenic.Scenic");
      FXL_CHECK(is_ok == ZX_OK);
    }

    // Set up Root Presenter inside the test environment.
    is_ok = services->AddServiceWithLaunchInfo({.url = kRootPresenter},
                                               "fuchsia.ui.input.InputDeviceRegistry");
    FXL_CHECK(is_ok == ZX_OK);

    is_ok =
        services->AddServiceWithLaunchInfo({.url = kRootPresenter}, "fuchsia.ui.policy.Presenter");
    FXL_CHECK(is_ok == ZX_OK);

    // Tunnel through some system services; these are needed for Scenic.
    is_ok = services->AllowParentService("fuchsia.sysmem.Allocator");
    FXL_CHECK(is_ok == ZX_OK);

    is_ok = services->AllowParentService("fuchsia.vulkan.loader.Loader");
    FXL_CHECK(is_ok == ZX_OK);

    test_env_ = CreateNewEnclosingEnvironment("touch_input_test_env", std::move(services),
                                              {.inherit_parent_services = true});

    WaitForEnclosingEnvToStart(test_env_.get());

    FXL_VLOG(1) << "Created test environment.";
  }

  ~TouchInputTest() override {
    FXL_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  sys::testing::EnclosingEnvironment* test_env() { return test_env_.get(); }

  scenic::Session* session() { return session_.get(); }
  void MakeSession(fuchsia::ui::scenic::SessionPtr session,
                   fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> session_listener) {
    session_ = std::make_unique<scenic::Session>(std::move(session), std::move(session_listener));
  }

  scenic::ViewHolder* view_holder() { return view_holder_.get(); }
  void MakeViewHolder(fuchsia::ui::views::ViewHolderToken token, const std::string& name) {
    FXL_CHECK(session_);
    view_holder_ = std::make_unique<scenic::ViewHolder>(session_.get(), std::move(token), name);
  }

  void SetRespondCallback(fit::function<void()> callback) {
    respond_callback_ = std::move(callback);
  }

  // |fuchsia::test::ui::ResponseListener|
  void Respond() override {
    FXL_CHECK(respond_callback_) << "Expected callback to be set for Respond().";
    respond_callback_();
  }

  // Inject directly into Root Presenter, using fuchsia.ui.input FIDLs.
  // TODO(48007): Switch to driver-based injection.
  void InjectInput() {
    using fuchsia::ui::input::InputReport;
    // Device parameters
    auto parameters = fuchsia::ui::input::TouchscreenDescriptor::New();
    *parameters = {.x = {.range = {.min = -1000, .max = 1000}},
                   .y = {.range = {.min = -1000, .max = 1000}},
                   .max_finger_id = 10};

    // Register it against Root Presenter.
    fuchsia::ui::input::DeviceDescriptor device{.touchscreen = std::move(parameters)};
    auto registry = test_env()->ConnectToService<fuchsia::ui::input::InputDeviceRegistry>();
    fuchsia::ui::input::InputDevicePtr connection;
    registry->RegisterDevice(std::move(device), connection.NewRequest());

    // Inject one input report, then a conclusion (empty) report.
    {
      auto touch = fuchsia::ui::input::TouchscreenReport::New();
      *touch = {.touches = {{.finger_id = 1, .x = 0, .y = 0}}};  // screen center
      // Use system clock, instead of dispatcher clock, for measurement purposes.
      InputReport report{.event_time = RealNow(), .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
    }

    {
      auto touch = fuchsia::ui::input::TouchscreenReport::New();
      InputReport report{.event_time = RealNow(), .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
    }

    ++injection_count_;
    FXL_LOG(INFO) << "*** Tap injected, count: " << injection_count_;
  }

  int injection_count() { return injection_count_; }

  static bool IsViewPropertiesChangedEvent(const ScenicEvent& event) {
    return event.Which() == ScenicEvent::Tag::kGfx &&
           event.gfx().Which() == GfxEvent::Tag::kViewPropertiesChanged;
  }

  static bool IsViewStateChangedEvent(const ScenicEvent& event) {
    return event.Which() == ScenicEvent::Tag::kGfx &&
           event.gfx().Which() == GfxEvent::Tag::kViewStateChanged;
  }

  static bool IsViewDisconnectedEvent(const ScenicEvent& event) {
    return event.Which() == ScenicEvent::Tag::kGfx &&
           event.gfx().Which() == GfxEvent::Tag::kViewDisconnected;
  }

 private:
  uint64_t RealNow() { return static_cast<uint64_t>(zx_clock_get_monotonic()); }

  fidl::Binding<fuchsia::test::ui::ResponseListener> response_listener_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;
  std::unique_ptr<scenic::Session> session_;
  int injection_count_ = 0;

  // Child view's ViewHolder.
  std::unique_ptr<scenic::ViewHolder> view_holder_;

  fit::function<void()> respond_callback_;
};

TEST_F(TouchInputTest, FlutterTap) {
  const std::string kOneFlutter = "fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cmx";

  // Define response when Flutter calls back with "Respond()".
  SetRespondCallback([this] {
    FXL_LOG(INFO) << "*** PASS ***";
    QuitLoop();
  });

  // Define when to set size for Flutter's view, and when to inject input against Flutter's view.
  scenic::Session::EventHandler handler = [this](std::vector<fuchsia::ui::scenic::Event> events) {
    for (const auto& event : events) {
      if (IsViewPropertiesChangedEvent(event)) {
        auto properties = event.gfx().view_properties_changed().properties;
        FXL_VLOG(1) << "Test received its view properties; transfer to child view: " << properties;
        FXL_CHECK(view_holder()) << "Expect that view holder is already set up.";
        view_holder()->SetViewProperties(properties);
        session()->Present(zx_clock_get_monotonic(), [](auto info) {});

      } else if (IsViewStateChangedEvent(event)) {
        bool hittable = event.gfx().view_state_changed().state.is_rendering;
        FXL_VLOG(1) << "Child's view content is hittable: " << std::boolalpha << hittable;
        if (hittable) {
          InjectInput();
        }

      } else if (IsViewDisconnectedEvent(event)) {
        // Save time, terminate the test immediately if we know that Flutter's view is borked.
        FXL_CHECK(injection_count() > 0)
            << "Expected to have completed input injection, but Flutter view terminated early.";
      }
    }
  };

  auto tokens_rt = scenic::ViewTokenPair::New();  // Root Presenter -> Test
  auto tokens_tf = scenic::ViewTokenPair::New();  // Test -> Flutter

  // Instruct Root Presenter to present test's View.
  auto root_presenter = test_env()->ConnectToService<fuchsia::ui::policy::Presenter>();
  root_presenter->PresentOrReplaceView(std::move(tokens_rt.view_holder_token),
                                       /* presentation */ nullptr);

  // Set up test's View, to harvest Flutter view's view_state.is_rendering signal.
  auto scenic = test_env()->ConnectToService<fuchsia::ui::scenic::Scenic>();
  auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get());
  MakeSession(std::move(session_pair.first), std::move(session_pair.second));
  session()->set_event_handler(std::move(handler));

  scenic::View view(session(), std::move(tokens_rt.view_token), "test's view");
  MakeViewHolder(std::move(tokens_tf.view_holder_token), "test's viewholder for flutter");
  view.AddChild(*view_holder());
  // Request to make test's view; this will trigger dispatch of view properties.
  session()->Present(zx_clock_get_monotonic(), [](auto info) {
    FXL_LOG(INFO) << "test's view and view holder created by Scenic.";
  });

  // Start Flutter app inside the test environment.
  // Note well. We launch the flutter component directly, and ask for its ViewProvider service
  // directly, to closely model production setup.
  fuchsia::sys::ComponentControllerPtr one_flutter_component;
  {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kOneFlutter;
    // Create a point-to-point offer-use connection between parent and child.
    auto child_services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
    one_flutter_component = test_env()->CreateComponent(std::move(launch_info));

    auto view_provider = child_services->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView(std::move(tokens_tf.view_token.value), /* in */ nullptr,
                              /* out */ nullptr);
  }

  // Post a "just in case" quit task, if the test hangs.
  async::PostDelayedTask(
      dispatcher(),
      [] { FXL_LOG(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
      kTimeout);

  RunLoop();  // Go!
}

}  // namespace
