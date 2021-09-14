// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/lifecycle/cpp/fidl.h>
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
#include <zircon/status.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <iostream>
#include <type_traits>

#include <gtest/gtest.h>
#include <test/touch/cpp/fidl.h>

// This test exercises the touch input dispatch path from Root Presenter to a Scenic client. It is a
// multi-component test, and carefully avoids sleeping or polling for component coordination.
// - It runs real Root Presenter and Scenic components.
// - It uses a fake display controller; the physical device is unused.
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
// - Test waits for the child to report a touch; when the test receives the report, the test quits
//   successfully.

namespace {

using test::touch::ResponseListener;
using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Common services for each test.
const std::map<std::string, std::string> LocalServices() {
  return {
      // Root presenter is included in this test's package so the two components have the same
      // /config/data. This allows the test to control the display rotation read by root presenter.
      {"fuchsia.ui.input.InputDeviceRegistry",
       "fuchsia-pkg://fuchsia.com/touch-input-test#meta/root_presenter.cmx"},
      {"fuchsia.ui.policy.Presenter",
       "fuchsia-pkg://fuchsia.com/touch-input-test#meta/root_presenter.cmx"},
      // Scenic protocols.
      {"fuchsia.ui.scenic.Scenic", "fuchsia-pkg://fuchsia.com/touch-input-test#meta/scenic.cmx"},
      {"fuchsia.ui.pointerinjector.Registry",
       "fuchsia-pkg://fuchsia.com/touch-input-test#meta/scenic.cmx"},  // For root_presenter
      // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
      {"fuchsia.ui.lifecycle.LifecycleController",
       "fuchsia-pkg://fuchsia.com/touch-input-test#meta/scenic.cmx"},
      // Misc protocols.
      {"fuchsia.cobalt.LoggerFactory",
       "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx"},
      {"fuchsia.hardware.display.Provider",
       "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx"},
  };
}

// Allow these global services from outside the test environment.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator",
          "fuchsia.scheduler.ProfileProvider"};
}

enum class TapLocation { kTopLeft, kTopRight };

class TouchInputBase : public gtest::TestWithEnvironmentFixture, public ResponseListener {
 protected:
  struct LaunchableService {
    std::string url;
    std::string name;
  };

  explicit TouchInputBase(const std::vector<LaunchableService>& extra_services) {
    auto services = TestWithEnvironmentFixture::CreateServices();

    // Key part of service setup: have this test component vend the |ResponseListener| service in
    // the constructed environment.
    {
      const zx_status_t is_ok =
          services->AddService<ResponseListener>(response_listener_.GetHandler(this));
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

    // Add test-specific launchable services.
    for (const auto& service_info : extra_services) {
      const zx_status_t is_ok =
          services->AddServiceWithLaunchInfo({.url = service_info.url}, service_info.name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service_info.name;
    }

    test_env_ = CreateNewEnclosingEnvironment("touch_input_test_env", std::move(services));
    WaitForEnclosingEnvToStart(test_env_.get());

    FX_VLOGS(1) << "Created test environment.";

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);
  }

  ~TouchInputBase() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  void SetUp() override {
    // Connects to scenic lifecycle controller in order to shutdown scenic at the end of the test.
    // This ensures the correct ordering of shutdown under CFv1: first scenic, then the fake display
    // controller.
    //
    // TODO(fxbug.dev/82655): Remove this after migrating to RealmBuilder.
    test_env()->ConnectToService<fuchsia::ui::lifecycle::LifecycleController>(
        scenic_lifecycle_controller_.NewRequest());

    // Get the display dimensions
    scenic_ = test_env()->ConnectToService<fuchsia::ui::scenic::Scenic>();
    scenic_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
    });
    scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
      display_width_ = display_info.width_in_px;
      display_height_ = display_info.height_in_px;
      FX_LOGS(INFO) << "Got display_width = " << display_width_
                    << " and display_height = " << display_height_;
    });
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });
  }

  // |testing::Test|
  void TearDown() override {
    scenic_.set_error_handler(nullptr);

    zx_status_t terminate_status = scenic_lifecycle_controller_->Terminate();
    FX_CHECK(terminate_status == ZX_OK)
        << "Failed to terminate Scenic with status: " << zx_status_get_string(terminate_status);
  }

  sys::testing::EnclosingEnvironment* test_env() { return test_env_.get(); }

  // Launches the client specified by |url|, listens for the client is_rendering signal and calls
  // |on_is_rendering| when it arrives.
  void LaunchClient(std::string url, std::string debug_name) {
    auto tokens_rt = scenic::ViewTokenPair::New();  // Root Presenter -> Test
    auto tokens_tf = scenic::ViewTokenPair::New();  // Test -> Client

    // Instruct Root Presenter to present test's View.
    auto root_presenter = test_env()->ConnectToService<fuchsia::ui::policy::Presenter>();
    root_presenter->PresentOrReplaceView(std::move(tokens_rt.view_holder_token),
                                         /* presentation */ nullptr);

    // Set up test's View, to harvest the client view's view_state.is_rendering signal.
    auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get());
    session_ = std::make_unique<scenic::Session>(std::move(session_pair.first),
                                                 std::move(session_pair.second));
    session_->SetDebugName(debug_name);
    bool is_rendering = false;
    session_->set_event_handler([this, debug_name, &is_rendering](
                                    const std::vector<fuchsia::ui::scenic::Event>& events) {
      for (const auto& event : events) {
        if (!event.is_gfx())
          continue;  // skip non-gfx events

        if (event.gfx().is_view_properties_changed()) {
          const auto properties = event.gfx().view_properties_changed().properties;
          FX_VLOGS(1) << "Test received its view properties; transfer to child view: "
                      << properties;
          FX_CHECK(view_holder_) << "Expect that view holder is already set up.";
          view_holder_->SetViewProperties(properties);
          session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});

        } else if (event.gfx().is_view_state_changed()) {
          is_rendering = event.gfx().view_state_changed().state.is_rendering;
          FX_VLOGS(1) << "Child's view content is rendering: " << std::boolalpha << is_rendering;
        } else if (event.gfx().is_view_disconnected()) {
          // Save time, terminate the test immediately if we know that client's view is borked.
          FX_CHECK(injection_count_ > 0) << "Expected to have completed input injection, but "
                                         << debug_name << " view terminated early.";
        }
      }
    });

    view_holder_ = std::make_unique<scenic::ViewHolder>(
        session_.get(), std::move(tokens_tf.view_holder_token), "test's view holder");
    view_ = std::make_unique<scenic::View>(session_.get(), std::move(tokens_rt.view_token),
                                           "test's view");
    view_->AddChild(*view_holder_);

    // Request to make test's view; this will trigger dispatch of view properties.
    session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {
      FX_VLOGS(1) << "test's view and view holder created by Scenic.";
    });

    // Start client app inside the test environment.
    // Note well. We launch the client component directly, and ask for its ViewProvider service
    // directly, to closely model production setup.
    {
      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = url;
      // Create a point-to-point offer-use connection between parent and child.
      child_services_ = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
      client_component_ = test_env()->CreateComponent(std::move(launch_info));

      auto view_provider = child_services_->Connect<fuchsia::ui::app::ViewProvider>();
      view_provider->CreateView(std::move(tokens_tf.view_token.value), /* in */ nullptr,
                                /* out */ nullptr);
    }

    RunLoopUntil([&is_rendering] { return is_rendering; });

    // Reset the event handler without capturing the is_rendering stack variable.
    session_->set_event_handler([this, debug_name](
                                    const std::vector<fuchsia::ui::scenic::Event>& events) {
      for (const auto& event : events) {
        if (!event.is_gfx())
          continue;  // skip non-gfx events

        if (event.gfx().is_view_properties_changed()) {
          const auto properties = event.gfx().view_properties_changed().properties;
          FX_VLOGS(1) << "Test received its view properties; transfer to child view: "
                      << properties;
          FX_CHECK(view_holder_) << "Expect that view holder is already set up.";
          view_holder_->SetViewProperties(properties);
          session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
        } else if (event.gfx().is_view_disconnected()) {
          // Save time, terminate the test immediately if we know that client's view is borked.
          FX_CHECK(injection_count_ > 0) << "Expected to have completed input injection, but "
                                         << debug_name << " view terminated early.";
        }
      }
    });
  }

  // Helper method for checking the test.touch.ResponseListener response from the client app.
  void SetResponseExpectations(float expected_x, float expected_y,
                               zx::basic_time<ZX_CLOCK_MONOTONIC>& input_injection_time,
                               std::string component_name, bool& injection_complete) {
    respond_callback_ = [expected_x, expected_y, component_name, &input_injection_time,
                         &injection_complete](test::touch::PointerData pointer_data) {
      EXPECT_EQ(pointer_data.component_name(), component_name);

      FX_LOGS(INFO) << "Client received tap at (" << pointer_data.local_x() << ", "
                    << pointer_data.local_y() << ").";
      FX_LOGS(INFO) << "Expected tap is at approximately (" << expected_x << ", " << expected_y
                    << ").";

      zx::duration elapsed_time =
          zx::basic_time<ZX_CLOCK_MONOTONIC>(pointer_data.time_received()) - input_injection_time;
      EXPECT_TRUE(elapsed_time.get() > 0 && elapsed_time.get() != ZX_TIME_INFINITE);
      FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
      FX_LOGS(INFO) << "Client Received Time (ns): " << pointer_data.time_received();
      FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();

      // Allow for minor rounding differences in coordinates.
      EXPECT_NEAR(pointer_data.local_x(), expected_x, 1);
      EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);

      injection_complete = true;
    };
  }

  // Calls test.touch.TestAppLauncher::Launch.
  // Only works if we've already launched a client that serves test.touch.TestAppLauncher.
  void LaunchEmbeddedClient(std::string component_url) {
    // Launch the embedded app.
    auto test_app_launcher = child_services().Connect<test::touch::TestAppLauncher>();
    bool child_launched = false;
    test_app_launcher->Launch(component_url, [&child_launched] { child_launched = true; });
    RunLoopUntil([&child_launched] { return child_launched; });

    // Waits an extra frame to avoid any flakes from the child launching signal firing slightly
    // early.
    bool frame_presented = false;
    session_->set_on_frame_presented_handler([&frame_presented](auto) { frame_presented = true; });
    session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
    RunLoopUntil([&frame_presented] { return frame_presented; });
    session_->set_on_frame_presented_handler([](auto) {});
  }

  // Inject directly into Root Presenter, using fuchsia.ui.input FIDLs.
  // Returns the timestamp on the first injected InputReport.
  template <typename TimeT>
  TimeT InjectInput(TapLocation tap_location) {
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
    FX_LOGS(INFO) << "Registered touchscreen with x touch range = (-1000, 1000) "
                  << "and y touch range = (-1000, 1000).";

    TimeT injection_time;

    {
      // Inject one input report, then a conclusion (empty) report.
      //
      // The /config/data/display_rotation (90) specifies how many degrees to rotate the
      // presentation child view, counter-clockwise, in a right-handed coordinate system. Thus,
      // the user observes the child view to rotate *clockwise* by that amount (90).
      //
      // Hence, a tap in the center of the display's top-right quadrant is observed by the child
      // view as a tap in the center of its top-left quadrant.
      auto touch = fuchsia::ui::input::TouchscreenReport::New();
      switch (tap_location) {
        case TapLocation::kTopLeft:
          // center of top right quadrant -> ends up as center of top left quadrant
          *touch = {.touches = {{.finger_id = 1, .x = 500, .y = -500}}};
          break;
        case TapLocation::kTopRight:
          // center of bottom right quadrant -> ends up as center of top right quadrant
          *touch = {.touches = {{.finger_id = 1, .x = 500, .y = 500}}};
          break;
        default:
          FX_NOTREACHED();
      }
      // Use system clock, instead of dispatcher clock, for measurement purposes.
      injection_time = RealNow<TimeT>();
      InputReport report{.event_time = TimeToUint(injection_time), .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
      FX_LOGS(INFO) << "Dispatching touch report at (500, -500)";
    }

    {
      auto touch = fuchsia::ui::input::TouchscreenReport::New();
      InputReport report{.event_time = TimeToUint(RealNow<TimeT>()),
                         .touchscreen = std::move(touch)};
      connection->DispatchReport(std::move(report));
    }

    ++injection_count_;
    FX_LOGS(INFO) << "*** Tap injected, count: " << injection_count_;

    return injection_time;
  }

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return display_width_; }
  uint32_t display_height() const { return display_height_; }

  fuchsia::sys::ComponentControllerPtr& client_component() { return client_component_; }
  sys::ServiceDirectory& child_services() { return *child_services_; }

  fit::function<void(test::touch::PointerData)> respond_callback_;

 private:
  // |test::touch::ResponseListener|
  void Respond(test::touch::PointerData pointer_data) override {
    FX_CHECK(respond_callback_) << "Expected callback to be set for test.touch.Respond().";
    respond_callback_(std::move(pointer_data));
  }

  template <typename TimeT>
  TimeT RealNow();

  template <>
  zx::time RealNow() {
    return zx::clock::get_monotonic();
  }

  template <>
  zx::time_utc RealNow() {
    zx::unowned_clock utc_clock(zx_utc_reference_get());
    zx_time_t now;
    FX_CHECK(utc_clock->read(&now) == ZX_OK);
    return zx::time_utc(now);
  }

  template <typename TimeT>
  uint64_t TimeToUint(const TimeT& time) {
    FX_CHECK(time.get() >= 0);
    return static_cast<uint64_t>(time.get());
  };

  fidl::BindingSet<test::touch::ResponseListener> response_listener_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;
  std::unique_ptr<scenic::Session> session_;
  int injection_count_ = 0;

  fuchsia::ui::lifecycle::LifecycleControllerSyncPtr scenic_lifecycle_controller_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  // Test view and child view's ViewHolder.
  std::unique_ptr<scenic::ViewHolder> view_holder_;
  std::unique_ptr<scenic::View> view_;

  fuchsia::sys::ComponentControllerPtr client_component_;
  std::shared_ptr<sys::ServiceDirectory> child_services_;
};

class TouchInputTest : public TouchInputBase {
 protected:
  TouchInputTest() : TouchInputBase({}) {}
};

TEST_F(TouchInputTest, FlutterTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient("fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cmx", "FlutterTap");

  bool injection_complete = false;
  SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                          input_injection_time,
                          /*component_name=*/"one-flutter", injection_complete);

  input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
  RunLoopUntil([&injection_complete] { return injection_complete; });
}

TEST_F(TouchInputTest, FlutterInFlutterTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  // Launch the embedding app.
  LaunchClient("fuchsia-pkg://fuchsia.com/embedding-flutter#meta/embedding-flutter.cmx",
               "FlutterInFlutterTap");

  // Launch the embedded app.
  LaunchEmbeddedClient("fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cmx");

  // Embedded app takes up the left half of the screen. Expect response from it when injecting to
  // the left.
  {
    bool injection_complete = false;
    SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                            /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                            input_injection_time,
                            /*component_name=*/"one-flutter", injection_complete);

    input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
    RunLoopUntil([&injection_complete] { return injection_complete; });
  }

  // Parent app takes up the right half of the screen. Expect response from it when injecting to the
  // right.
  {
    bool injection_complete = false;
    SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) * (3.f / 4.f),
                            /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                            input_injection_time,
                            /*component_name=*/"embedding-flutter", injection_complete);

    input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopRight);
    RunLoopUntil([&injection_complete] { return injection_complete; });
  }
}

TEST_F(TouchInputTest, CppGfxClientTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient("fuchsia-pkg://fuchsia.com/touch-gfx-client#meta/touch-gfx-client.cmx",
               "CppGfxClientTap");

  bool injection_complete = false;
  SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                          input_injection_time,
                          /*component_name=*/"touch-gfx-client", injection_complete);

  input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
  RunLoopUntil([&injection_complete] { return injection_complete; });
}

class WebEngineTest : public TouchInputBase {
 public:
  WebEngineTest()
      : TouchInputBase({
            {.url = kFontsProvider, .name = fuchsia::fonts::Provider::Name_},
            {.url = kIntl, .name = fuchsia::intl::PropertyProvider::Name_},
            {.url = kMemoryPressureProvider, .name = fuchsia::memorypressure::Provider::Name_},
            {.url = kNetstack, .name = fuchsia::netstack::Netstack::Name_},
            {.url = kNetstack, .name = fuchsia::net::interfaces::State::Name_},
            {.url = kSemanticsManager,
             .name = fuchsia::accessibility::semantics::SemanticsManager::Name_},
            {.url = kTextManager, .name = fuchsia::ui::input::ImeService::Name_},
            {.url = kTextManager, .name = fuchsia::ui::input::ImeVisibilityService::Name_},
            {.url = kWebContextProvider, .name = fuchsia::web::ContextProvider::Name_},
        }) {}

 protected:
  // Injects an input event, and posts a task to retry after `kTapRetryInterval`.
  //
  // We post the retry task because the first input event we send to WebEngine may be lost.
  // The reason the first event may be lost is that there is a race condition as the WebEngine
  // starts up.
  //
  // More specifically: in order for our web app's JavaScript code (see kAppCode in one-chromium.cc)
  // to receive the injected input, two things must be true before we inject the input:
  // * The WebEngine must have installed its `render_node_`, and
  // * The WebEngine must have set the shape of its `input_node_`
  //
  // The problem we have is that the `is_rendering` signal that we monitor only guarantees us
  // the `render_node_` is ready. If the `input_node_` is not ready at that time, Scenic will
  // find that no node was hit by the touch, and drop the touch event.
  //
  // As for why `is_rendering` triggers before there's any hittable element, that falls out of
  // the way WebEngine constructs its scene graph. Namely, the `render_node_` has a shape, so
  // that node `is_rendering` as soon as it is `Present()`-ed. Walking transitively up the
  // scene graph, that causes our `Session` to receive the `is_rendering` signal.
  //
  // For more detals, see fxbug.dev/57268.
  //
  // TODO(fxbug.dev/58322): Improve synchronization when we move to Flatland.
  void TryInject(zx::basic_time<ZX_CLOCK_UTC>* input_injection_time) {
    *input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_UTC>>(TapLocation::kTopLeft);
    async::PostDelayedTask(
        dispatcher(), [this, input_injection_time] { TryInject(input_injection_time); },
        kTapRetryInterval);
  };

  // Helper method for checking the test.touch.ResponseListener response from a web app.
  void SetResponseExpectationsWeb(float expected_x, float expected_y,
                                  zx::basic_time<ZX_CLOCK_UTC>& input_injection_time,
                                  std::string component_name, bool& injection_complete) {
    respond_callback_ = [expected_x, expected_y, component_name, &injection_complete,
                         &input_injection_time](test::touch::PointerData pointer_data) {
      EXPECT_EQ(pointer_data.component_name(), component_name);

      // Convert Chromium's position, which is in logical pixels, to a position in physical
      // pixels. Note that Chromium reports integer values, so this conversion introduces an
      // error of up to `device_pixel_ratio`.
      auto device_pixel_ratio = pointer_data.device_pixel_ratio();
      auto chromium_x = pointer_data.local_x();
      auto chromium_y = pointer_data.local_y();
      auto device_x = chromium_x * device_pixel_ratio;
      auto device_y = chromium_y * device_pixel_ratio;

      FX_LOGS(INFO) << "Chromium reported tap at (" << chromium_x << ", " << chromium_y << ").";
      FX_LOGS(INFO) << "Tap scaled to (" << device_x << ", " << device_y << ").";
      FX_LOGS(INFO) << "Expected tap is at approximately (" << expected_x << ", " << expected_y
                    << ").";

      zx::duration elapsed_time =
          zx::basic_time<ZX_CLOCK_UTC>(pointer_data.time_received()) - input_injection_time;
      EXPECT_NE(elapsed_time.get(), ZX_TIME_INFINITE);
      FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
      FX_LOGS(INFO) << "Chromium Received Time (ns): " << pointer_data.time_received();
      FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();

      // Allow for minor rounding differences in coordinates. As noted above, `device_x` and
      // `device_y` may have an error of up to `device_pixel_ratio` physical pixels.
      EXPECT_NEAR(device_x, expected_x, device_pixel_ratio);
      EXPECT_NEAR(device_y, expected_y, device_pixel_ratio);

      injection_complete = true;
    };
  }

 private:
  static constexpr char kFontsProvider[] = "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx";
  static constexpr char kIntl[] =
      "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cmx";
  static constexpr char kMemoryPressureProvider[] =
      "fuchsia-pkg://fuchsia.com/memory_monitor#meta/memory_monitor.cmx";
  static constexpr char kNetstack[] = "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
  static constexpr char kSemanticsManager[] =
      "fuchsia-pkg://fuchsia.com/a11y-manager#meta/a11y-manager.cmx";
  static constexpr char kTextManager[] =
      "fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx";
  static constexpr char kWebContextProvider[] =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";

  // The typical latency on devices we've tested is ~60 msec. The retry interval is chosen to be
  // a) Long enough that it's unlikely that we send a new tap while a previous tap is still being
  //    processed. That is, it should be far more likely that a new tap is sent because the first
  //    tap was lost, than because the system is just running slowly.
  // b) Short enough that we don't slow down tryjobs.
  //
  // The first property is important to avoid skewing the latency metrics that we collect.
  // For an explanation of why a tap might be lost, see the documentation for TryInject().
  static constexpr auto kTapRetryInterval = zx::sec(1);
};

TEST_F(WebEngineTest, ChromiumTap) {
  // Use `ZX_CLOCK_UTC` for compatibility with the time reported by `Date.now()` in web-engine.
  zx::basic_time<ZX_CLOCK_UTC> input_injection_time(0);

  // Note well: unlike one-flutter and cpp-gfx-client, the web app may be rendering before
  // it is hittable. Nonetheless, waiting for rendering is better than injecting the touch
  // immediately. In the event that the app is not hittable, `TryInject()` will retry.
  LaunchClient("fuchsia-pkg://fuchsia.com/one-chromium#meta/one-chromium.cmx", "ChromiumTap");
  client_component().events().OnTerminated = [](int64_t return_code,
                                                fuchsia::sys::TerminationReason reason) {
    // Unlike the Flutter and C++ apps, the process hosting the web app's logic doesn't retain
    // the view token for the life of the app (the process passes that token on to the web engine
    // process). Consequently, we can't just rely on the IsViewDisconnected message to detect
    // early termination of the app.
    if (return_code != 0) {
      FX_LOGS(FATAL) << "One-Chromium terminated abnormally with return_code=" << return_code
                     << ", reason="
                     << static_cast<std::underlying_type_t<decltype(reason)>>(reason);
    }
  };

  bool injection_complete = false;
  SetResponseExpectationsWeb(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                             /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                             input_injection_time,
                             /*component_name=*/"one-chromium", injection_complete);

  TryInject(&input_injection_time);
  RunLoopUntil([&injection_complete] { return injection_complete; });
}

TEST_F(WebEngineTest, WebInFlutterTap) {
  // Launch the embedding app.
  LaunchClient("fuchsia-pkg://fuchsia.com/embedding-flutter#meta/embedding-flutter.cmx",
               "WebInFlutterTap");

  // Launch the embedded app.
  LaunchEmbeddedClient("fuchsia-pkg://fuchsia.com/one-chromium#meta/one-chromium.cmx");

  // Parent app takes up the right half of the screen. Expect response from it when injecting to the
  // right.
  {
    // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
    zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

    bool injection_complete = false;
    SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) * (3.f / 4.f),
                            /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                            input_injection_time,
                            /*component_name=*/"embedding-flutter", injection_complete);

    input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopRight);
    RunLoopUntil([&injection_complete] { return injection_complete; });
  }

  // Embedded app takes up the left half of the screen. Expect response from it when injecting to
  // the left.
  {
    // Use `ZX_CLOCK_UTC` for compatibility with the time reported by `Date.now()` in web-engine.
    zx::basic_time<ZX_CLOCK_UTC> input_injection_time(0);

    bool injection_complete = false;
    SetResponseExpectationsWeb(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                               /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                               input_injection_time,
                               /*component_name=*/"one-chromium", injection_complete);

    TryInject(&input_injection_time);
    RunLoopUntil([&injection_complete] { return injection_complete; });
  }
}

}  // namespace
