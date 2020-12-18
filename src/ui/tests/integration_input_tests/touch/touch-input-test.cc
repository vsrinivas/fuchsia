// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/test/ui/cpp/fidl.h>
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
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <iostream>
#include <type_traits>

#include <gtest/gtest.h>

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
// - Test waits for the child to report a touch; when the test receives the report, the test quits
//   successfully.

namespace {

using fuchsia::test::ui::ResponseListener;
using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Fuchsia components that this test launches.
// Root presenter is included in this test's package so the two components have the same
// /config/data. This allows the test to control the display rotation read by root presenter.
constexpr char kRootPresenter[] =
    "fuchsia-pkg://fuchsia.com/touch-input-test#meta/root_presenter.cmx";
constexpr char kScenic[] = "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx";

class TouchInputBase : public sys::testing::TestWithEnvironment, public ResponseListener {
 protected:
  struct LaunchableService {
    std::string url;
    std::string name;
  };

  explicit TouchInputBase(const std::vector<LaunchableService>& extra_services)
      : response_listener_(this) {
    bool trace_provider_already_started = false;
    if (!trace::TraceProviderWithFdio::CreateSynchronously(
            dispatcher(), "touch-input-test", &trace_provider_, &trace_provider_already_started)) {
      FX_LOGS(ERROR) << "Trace provider registration failed.";
    }

    auto services = sys::testing::EnvironmentServices::Create(real_env());
    zx_status_t is_ok;

    // Key part of service setup: have this test component vend the |ResponseListener| service in
    // the constructed environment.
    is_ok = services->AddService<ResponseListener>(
        [this](fidl::InterfaceRequest<ResponseListener> request) {
          response_listener_.Bind(std::move(request));
        });
    FX_CHECK(is_ok == ZX_OK);

    // Set up Scenic inside the test environment.
    {
      auto launch_info_provider = []() {
        fuchsia::sys::LaunchInfo launch_info;
        launch_info.url = kScenic;
        if (FX_VLOG_IS_ON(1)) {
          launch_info.arguments.emplace();
          launch_info.arguments->push_back("--verbose=2");
        }
        return launch_info;
      };

      is_ok = services->AddServiceWithLaunchInfo(kScenic, launch_info_provider,
                                                 fuchsia::ui::scenic::Scenic::Name_);
      FX_CHECK(is_ok == ZX_OK);

      // Scenic needs access to the fuchsia.hardware.display.Provider service, which is typically
      // and in this case provided by Scenic itself.
      is_ok = services->AddServiceWithLaunchInfo(kScenic, launch_info_provider,
                                                 fuchsia::hardware::display::Provider::Name_);
      FX_CHECK(is_ok == ZX_OK);
      is_ok = services->AddServiceWithLaunchInfo({.url = kScenic},
                                                 fuchsia::ui::pointerinjector::Registry::Name_);
      FX_CHECK(is_ok == ZX_OK);
    }

    // Set up Root Presenter inside the test environment.
    is_ok = services->AddServiceWithLaunchInfo({.url = kRootPresenter},
                                               fuchsia::ui::input::InputDeviceRegistry::Name_);
    FX_CHECK(is_ok == ZX_OK);

    is_ok = services->AddServiceWithLaunchInfo({.url = kRootPresenter},
                                               fuchsia::ui::policy::Presenter::Name_);
    FX_CHECK(is_ok == ZX_OK);

    // Add test-specific launchable services.
    for (const auto& service_info : extra_services) {
      is_ok = services->AddServiceWithLaunchInfo({.url = service_info.url}, service_info.name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service_info.name;
    }

    test_env_ = CreateNewEnclosingEnvironment("touch_input_test_env", std::move(services),
                                              {.inherit_parent_services = true});

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

  sys::testing::EnclosingEnvironment* test_env() { return test_env_.get(); }

  scenic::Session* session() { return session_.get(); }
  void MakeSession(fuchsia::ui::scenic::SessionPtr session,
                   fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> session_listener) {
    session_ = std::make_unique<scenic::Session>(std::move(session), std::move(session_listener));
  }

  scenic::ViewHolder* view_holder() { return view_holder_.get(); }
  void MakeViewHolder(fuchsia::ui::views::ViewHolderToken token, const std::string& name) {
    FX_CHECK(session_);
    view_holder_ = std::make_unique<scenic::ViewHolder>(session_.get(), std::move(token), name);
  }

  void SetRespondCallback(fit::function<void(fuchsia::test::ui::PointerData)> callback) {
    respond_callback_ = std::move(callback);
  }

  // |fuchsia::test::ui::ResponseListener|
  void Respond(fuchsia::test::ui::PointerData pointer_data) override {
    FX_CHECK(respond_callback_) << "Expected callback to be set for Respond().";
    respond_callback_(std::move(pointer_data));
  }

  // Inject directly into Root Presenter, using fuchsia.ui.input FIDLs.
  // Returns the timestamp on the first injected InputReport.
  template <typename TimeT>
  TimeT InjectInput() {
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
      // RotateTouchEvent() depends on this sending a touch event at the same location.
      auto touch = fuchsia::ui::input::TouchscreenReport::New();
      *touch = {
          .touches = {{.finger_id = 1, .x = 500, .y = -500}}};  // center of top right quadrant
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

  int injection_count() const { return injection_count_; }

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
  template <typename TimeT>
  TimeT RealNow() {
    TimeT now(ZX_TIME_INFINITE_PAST);
    FX_CHECK(zx::clock::get(&now) == ZX_OK);
    return now;
  };

  template <typename TimeT>
  uint64_t TimeToUint(const TimeT& time) {
    FX_CHECK(time.get() >= 0);
    return static_cast<uint64_t>(time.get());
  };

  fidl::Binding<fuchsia::test::ui::ResponseListener> response_listener_;
  std::unique_ptr<trace::TraceProviderWithFdio> trace_provider_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> test_env_;
  std::unique_ptr<scenic::Session> session_;
  int injection_count_ = 0;

  // Child view's ViewHolder.
  std::unique_ptr<scenic::ViewHolder> view_holder_;

  fit::function<void(fuchsia::test::ui::PointerData)> respond_callback_;
};

class TouchInputTest : public TouchInputBase {
 protected:
  TouchInputTest() : TouchInputBase({}) {}
};

TEST_F(TouchInputTest, FlutterTap) {
  TRACE_DURATION("touch-input-test", "TouchInputTest::FlutterTap");
  const std::string kOneFlutter = "fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cmx";
  uint32_t display_width = 0;
  uint32_t display_height = 0;

  // Get the display dimensions
  auto scenic = test_env()->ConnectToService<fuchsia::ui::scenic::Scenic>();
  scenic->GetDisplayInfo(
      [&display_width, &display_height](fuchsia::ui::gfx::DisplayInfo display_info) {
        display_width = display_info.width_in_px;
        display_height = display_info.height_in_px;
        FX_LOGS(INFO) << "Got display_width = " << display_width
                      << " and display_height = " << display_height;
      });
  RunLoopUntil(
      [&display_width, &display_height] { return display_width != 0 && display_height != 0; });

  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  // Define test expectations for when Flutter calls back with "Respond()".
  SetRespondCallback([this, display_width, display_height,
                      &input_injection_time](fuchsia::test::ui::PointerData pointer_data) {
    // The /config/data/display_rotation (90) specifies how many degrees to rotate the
    // presentation child view, counter-clockwise, in a right-handed coordinate system. Thus,
    // the user observes the child view to rotate *clockwise* by that amount (90).
    //
    // Hence, a tap in the center of the display's top-right quadrant is observed by the child
    // view as a tap in the center of its top-left quadrant.
    float expected_x = display_height / 4.f;
    float expected_y = display_width / 4.f;

    FX_LOGS(INFO) << "Flutter received tap at (" << pointer_data.local_x() << ", "
                  << pointer_data.local_y() << ").";
    FX_LOGS(INFO) << "Expected tap is at approximately (" << expected_x << ", " << expected_y
                  << ").";

    zx::duration elapsed_time =
        zx::basic_time<ZX_CLOCK_MONOTONIC>(pointer_data.time_received()) - input_injection_time;
    EXPECT_TRUE(elapsed_time.get() > 0 && elapsed_time.get() != ZX_TIME_INFINITE);
    FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
    FX_LOGS(INFO) << "Flutter Received Time (ns): " << pointer_data.time_received();
    FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();
    TRACE_INSTANT("touch-input-test", "input_latency", TRACE_SCOPE_PROCESS, "input_injection_time",
                  input_injection_time.get(), "flutter_received_time", pointer_data.time_received(),
                  "elapsed_time", elapsed_time.to_nsecs());

    // Allow for minor rounding differences in coordinates.
    EXPECT_NEAR(pointer_data.local_x(), expected_x, 1);
    EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);

    FX_LOGS(INFO) << "*** PASS ***";
    QuitLoop();
  });

  // Define when to set size for Flutter's view, and when to inject input against Flutter's view.
  scenic::Session::EventHandler handler =
      [this, &input_injection_time](const std::vector<fuchsia::ui::scenic::Event>& events) {
        for (const auto& event : events) {
          if (IsViewPropertiesChangedEvent(event)) {
            auto properties = event.gfx().view_properties_changed().properties;
            FX_VLOGS(1) << "Test received its view properties; transfer to child view: "
                        << properties;
            FX_CHECK(view_holder()) << "Expect that view holder is already set up.";
            view_holder()->SetViewProperties(properties);
            session()->Present(zx_clock_get_monotonic(), [](auto info) {});

          } else if (IsViewStateChangedEvent(event)) {
            bool hittable = event.gfx().view_state_changed().state.is_rendering;
            FX_VLOGS(1) << "Child's view content is hittable: " << std::boolalpha << hittable;
            if (hittable) {
              input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>();
            }

          } else if (IsViewDisconnectedEvent(event)) {
            // Save time, terminate the test immediately if we know that Flutter's view is borked.
            FX_CHECK(injection_count() > 0)
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
  auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get());
  MakeSession(std::move(session_pair.first), std::move(session_pair.second));
  session()->set_event_handler(std::move(handler));
  session()->SetDebugName("flutter-tap-test");

  scenic::View view(session(), std::move(tokens_rt.view_token), "test's view");
  MakeViewHolder(std::move(tokens_tf.view_holder_token), "test's viewholder for flutter");
  view.AddChild(*view_holder());
  // Request to make test's view; this will trigger dispatch of view properties.
  session()->Present(zx_clock_get_monotonic(), [](auto info) {
    FX_VLOGS(1) << "test's view and view holder created by Scenic.";
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

  RunLoop();  // Go!
}

TEST_F(TouchInputTest, CppGfxClientTap) {
  const std::string kCppGfxClient =
      "fuchsia-pkg://fuchsia.com/cpp-gfx-client#meta/cpp-gfx-client.cmx";
  uint32_t display_width = 0;
  uint32_t display_height = 0;

  // Get the display dimensions
  auto scenic = test_env()->ConnectToService<fuchsia::ui::scenic::Scenic>();
  scenic->GetDisplayInfo(
      [&display_width, &display_height](fuchsia::ui::gfx::DisplayInfo display_info) {
        display_width = display_info.width_in_px;
        display_height = display_info.height_in_px;
        FX_LOGS(INFO) << "Got display_width = " << display_width
                      << " and display_height = " << display_height;
      });
  RunLoopUntil(
      [&display_width, &display_height] { return display_width != 0 && display_height != 0; });

  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  // Define test expectations for when CppGfxClient calls back with "Respond()".
  SetRespondCallback([this, display_width, display_height,
                      &input_injection_time](fuchsia::test::ui::PointerData pointer_data) {
    // The /config/data/display_rotation (90) specifies how many degrees to rotate the
    // presentation child view, counter-clockwise, in a right-handed coordinate system. Thus,
    // the user observes the child view to rotate *clockwise* by that amount (90).
    //
    // Hence, a tap in the center of the display's top-right quadrant is observed by the child
    // view as a tap in the center of its top-left quadrant.
    float expected_x = display_height / 4.f;
    float expected_y = display_width / 4.f;

    FX_LOGS(INFO) << "CppGfxClient received tap at (" << pointer_data.local_x() << ", "
                  << pointer_data.local_y() << ").";
    FX_LOGS(INFO) << "Expected tap is at approximately (" << expected_x << ", " << expected_y
                  << ").";

    zx::duration elapsed_time =
        zx::basic_time<ZX_CLOCK_MONOTONIC>(pointer_data.time_received()) - input_injection_time;
    EXPECT_TRUE(elapsed_time.get() > 0 && elapsed_time.get() != ZX_TIME_INFINITE);
    FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
    FX_LOGS(INFO) << "CppGfxClient Received Time (ns): " << pointer_data.time_received();
    FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();

    // Allow for minor rounding differences in coordinates.
    EXPECT_NEAR(pointer_data.local_x(), expected_x, 1);
    EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);

    FX_LOGS(INFO) << "*** PASS ***";
    QuitLoop();
  });

  // Define when to set size for CppGfxClient's view, and when to inject input against
  // CppGfxClient's view.
  scenic::Session::EventHandler handler =
      [this, &input_injection_time](const std::vector<fuchsia::ui::scenic::Event>& events) {
        for (const auto& event : events) {
          if (IsViewPropertiesChangedEvent(event)) {
            auto properties = event.gfx().view_properties_changed().properties;
            FX_VLOGS(1) << "Test received its view properties; transfer to child view: "
                        << properties;
            FX_CHECK(view_holder()) << "Expect that view holder is already set up.";
            view_holder()->SetViewProperties(properties);
            session()->Present(zx_clock_get_monotonic(), [](auto info) {});

          } else if (IsViewStateChangedEvent(event)) {
            bool hittable = event.gfx().view_state_changed().state.is_rendering;
            FX_VLOGS(1) << "Child's view content is hittable: " << std::boolalpha << hittable;
            if (hittable) {
              input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>();
            }

          } else if (IsViewDisconnectedEvent(event)) {
            // Save time, terminate the test immediately if we know that CppGfxClient's view is
            // borked.
            FX_CHECK(injection_count() > 0) << "Expected to have completed input injection, but "
                                               "CppGfxClient's view terminated early.";
          }
        }
      };

  auto tokens_rt = scenic::ViewTokenPair::New();  // Root Presenter -> Test
  auto tokens_tf = scenic::ViewTokenPair::New();  // Test -> CppGfxClient

  // Instruct Root Presenter to present test's View.
  auto root_presenter = test_env()->ConnectToService<fuchsia::ui::policy::Presenter>();
  root_presenter->PresentOrReplaceView(std::move(tokens_rt.view_holder_token),
                                       /* presentation */ nullptr);

  // Set up test's View, to harvest CppGfxClient view's view_state.is_rendering signal.
  auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get());
  MakeSession(std::move(session_pair.first), std::move(session_pair.second));
  session()->set_event_handler(std::move(handler));
  session()->SetDebugName("cpp-gfx-client-tap-test");

  scenic::View view(session(), std::move(tokens_rt.view_token), "test's view");
  MakeViewHolder(std::move(tokens_tf.view_holder_token), "test's viewholder for CppGfxClient");
  view.AddChild(*view_holder());
  // Request to make test's view; this will trigger dispatch of view properties.
  session()->Present(zx_clock_get_monotonic(), [](auto info) {
    FX_LOGS(INFO) << "test's view and view holder created by Scenic.";
  });

  // Start CppGfxClient app inside the test environment.
  // Note well. We launch the CppGfxClient component directly, and ask for its ViewProvider service
  // directly, to closely model production setup.
  fuchsia::sys::ComponentControllerPtr cpp_gfx_client_component;
  {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kCppGfxClient;
    // Create a point-to-point offer-use connection between parent and child.
    auto child_services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
    cpp_gfx_client_component = test_env()->CreateComponent(std::move(launch_info));

    auto view_provider = child_services->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView(std::move(tokens_tf.view_token.value), /* in */ nullptr,
                              /* out */ nullptr);
  }

  RunLoop();  // Go!
}

class WebEngineTest : public TouchInputBase {
 public:
  WebEngineTest()
      : TouchInputBase({
            {.url = kFontsProvider, .name = fuchsia::fonts::Provider::Name_},
            {.url = kImeService, .name = fuchsia::ui::input::ImeService::Name_},
            {.url = kImeService, .name = fuchsia::ui::input::ImeVisibilityService::Name_},
            {.url = kMemoryPressureProvider, .name = fuchsia::memorypressure::Provider::Name_},
            {.url = kNetstack, .name = fuchsia::netstack::Netstack::Name_},
            {.url = kSemanticsManager,
             .name = fuchsia::accessibility::semantics::SemanticsManager::Name_},
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
    *input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_UTC>>();
    async::PostDelayedTask(
        dispatcher(), [this, input_injection_time] { TryInject(input_injection_time); },
        kTapRetryInterval);
  };

 private:
  static constexpr char kFontsProvider[] = "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx";
  static constexpr char kImeService[] =
      "fuchsia-pkg://fuchsia.com/ime_service#meta/ime_service.cmx";
  static constexpr char kMemoryPressureProvider[] =
      "fuchsia-pkg://fuchsia.com/memory_monitor#meta/memory_monitor.cmx";
  static constexpr char kNetstack[] = "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx";
  static constexpr char kWebContextProvider[] =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";
  static constexpr char kSemanticsManager[] =
      "fuchsia-pkg://fuchsia.com/a11y_manager#meta/a11y_manager.cmx";

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
  const std::string kOneChromium = "fuchsia-pkg://fuchsia.com/one-chromium#meta/one-chromium.cmx";
  uint32_t display_width = 0;
  uint32_t display_height = 0;

  // Get the display dimensions
  auto scenic = test_env()->ConnectToService<fuchsia::ui::scenic::Scenic>();
  scenic->GetDisplayInfo(
      [&display_width, &display_height](fuchsia::ui::gfx::DisplayInfo display_info) {
        display_width = display_info.width_in_px;
        display_height = display_info.height_in_px;
        FX_LOGS(INFO) << "Got display_width = " << display_width
                      << " and display_height = " << display_height;
      });
  RunLoopUntil(
      [&display_width, &display_height] { return display_width != 0 && display_height != 0; });

  // Use `ZX_CLOCK_UTC` for compatibility with the time reported by `Date.now()` in web-engine.
  zx::basic_time<ZX_CLOCK_UTC> input_injection_time(0);

  // Define test expectations for when Chromium calls back with "Respond()".
  SetRespondCallback([this, display_width, display_height,
                      &input_injection_time](fuchsia::test::ui::PointerData pointer_data) {
    // The /config/data/display_rotation (90) specifies how many degrees to rotate the
    // presentation child view, counter-clockwise, in a right-handed coordinate system. Thus,
    // the user observes the child view to rotate *clockwise* by that amount (90).
    //
    // Hence, a tap in the center of the display's top-right quadrant is observed by the child
    // view as a tap in the center of its top-left quadrant.
    float expected_x = display_height / 4.f;
    float expected_y = display_width / 4.f;

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

    QuitLoop();
  });

  // Define when to set size for Chromium's view, and when to inject input against Chromium's view.
  scenic::Session::EventHandler handler = [this, &input_injection_time](
                                              const std::vector<fuchsia::ui::scenic::Event>&
                                                  events) {
    for (const auto& event : events) {
      if (IsViewPropertiesChangedEvent(event)) {
        auto properties = event.gfx().view_properties_changed().properties;
        FX_VLOGS(1) << "Test received its view properties; transfer to child view: " << properties;
        FX_CHECK(view_holder()) << "Expect that view holder is already set up.";
        view_holder()->SetViewProperties(properties);
        session()->Present(zx_clock_get_monotonic(), [](auto info) {});
      } else if (IsViewStateChangedEvent(event)) {
        // Note well: unlike one-flutter and cpp-gfx-client, the web app may be rendering before
        // it is hittable. Nonetheless, waiting for rendering is better than injecting the touch
        // immediately. In the event that the app is not hittable, `TryInject()` will retry.
        bool rendering = event.gfx().view_state_changed().state.is_rendering;
        FX_VLOGS(1) << "Child's view content is rendering: " << std::boolalpha << rendering;
        if (rendering) {
          TryInject(&input_injection_time);
        }
      } else if (IsViewDisconnectedEvent(event)) {
        // Save time, terminate the test immediately if we know that Chromium's view is borked.
        FX_CHECK(injection_count() > 0)
            << "Expected to have completed input injection, but Chromium view terminated early.";
      }
    }
  };

  auto tokens_rt = scenic::ViewTokenPair::New();  // Root Presenter -> Test
  auto tokens_tc = scenic::ViewTokenPair::New();  // Test -> Chromium

  // Instruct Root Presenter to present test's View.
  auto root_presenter = test_env()->ConnectToService<fuchsia::ui::policy::Presenter>();
  root_presenter->PresentOrReplaceView(std::move(tokens_rt.view_holder_token),
                                       /* presentation */ nullptr);

  // Set up test's View, to harvest Chromium view's view_state.is_rendering signal.
  auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get());
  MakeSession(std::move(session_pair.first), std::move(session_pair.second));
  session()->set_event_handler(std::move(handler));
  session()->SetDebugName("chromium-tap-test");

  scenic::View view(session(), std::move(tokens_rt.view_token), "test's view");
  MakeViewHolder(std::move(tokens_tc.view_holder_token), "test's viewholder for chromium");
  view.AddChild(*view_holder());
  // Request to make test's view; this will trigger dispatch of view properties.
  session()->Present(zx_clock_get_monotonic(), [](auto info) {
    FX_VLOGS(1) << "test's view and view holder created by Scenic.";
  });

  // Start Chromium app inside the test environment.
  fuchsia::sys::ComponentControllerPtr one_chromium_component;
  {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kOneChromium;
    // Create a point-to-point offer-use connection between parent and child.
    auto child_services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
    one_chromium_component = test_env()->CreateComponent(std::move(launch_info));
    one_chromium_component.events().OnTerminated = [](int64_t return_code,
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

    auto view_provider = child_services->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView(std::move(tokens_tc.view_token.value), /* in */ nullptr,
                              /* out */ nullptr);
  }

  RunLoop();  // Go!
}

}  // namespace
