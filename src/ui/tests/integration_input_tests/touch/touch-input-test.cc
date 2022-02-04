// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <src/lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <test/touch/cpp/fidl.h>

#include "fuchsia/sysmem/cpp/fidl.h"

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
//
// This test uses the realm_builder library to construct the topology of components
// and routes services between them. For v2 components, every test driver component
// sits as a child of test_manager in the topology. Thus, the topology of a test
// driver component such as this one looks like this:
//
//     test_manager
//         |
//   touch-input-test.cml (this component)
//
// With the usage of the realm_builder library, we construct a realm during runtime
// and then extend the topology to look like:
//
//    test_manager
//         |
//   touch-input-test.cml (this component)
//         |
//   <created realm root>
//      /      \
//   scenic  root_presenter
//
// For more information about testing v2 components and realm_builder,
// visit the following links:
//
// Testing: https://fuchsia.dev/fuchsia-src/development/testing/components
// Realm Builder: https://fuchsia.dev/fuchsia-src/development/testing/components/realm_builder

namespace {

using test::touch::ResponseListener;
using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmRoot;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

// Alias for Component child name as provided to Realm Builder.
using ChildName = std::string;

// Alias for Component Legacy URL as provided to Realm Builder.
using LegacyUrl = std::string;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

constexpr auto kRootPresenter = "root_presenter";
constexpr auto kScenicTestRealm = "scenic-test-realm";
constexpr auto kMockResponseListener = "response_listener";

enum class TapLocation { kTopLeft, kTopRight };

// The type used to measure UTC time. The integer value here does not matter so
// long as it differs from the ZX_CLOCK_MONOTONIC=0 defined by Zircon.
using time_utc = zx::basic_time<1>;

// Components used by all tests. These will be installed as direct children of
// the root component of the realm. In v2, every protocol must be *explicitly*
// routed from one source to a target. In this case, these base components
// provide capabilities to be used either by the client components, e.g. OneFlutter,
// created below, or by this component. Note, that when I refer to "this component",
// I'm referring to the test suite, which is itself a component.
void AddBaseComponents(RealmBuilder* realm_builder) {
  realm_builder->AddLegacyChild(
      kRootPresenter, "fuchsia-pkg://fuchsia.com/touch-input-test#meta/root_presenter.cmx");
  realm_builder->AddChild(kScenicTestRealm,
                          "fuchsia-pkg://fuchsia.com/touch-input-test#meta/scenic-test-realm.cm");
}

void AddBaseRoutes(RealmBuilder* realm_builder) {
  // Capabilities routed from test_manager to components in realm.
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kScenicTestRealm}}});
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kRootPresenter}}});

  // Capabilities routed between siblings in realm.
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                             Protocol{fuchsia::ui::pointerinjector::Registry::Name_}},
            .source = ChildRef{kScenicTestRealm},
            .targets = {ChildRef{kRootPresenter}}});

  // Capabilities routed up to test driver (this component).
  realm_builder->AddRoute(
      Route{.capabilities = {Protocol{fuchsia::ui::input::InputDeviceRegistry::Name_},
                             Protocol{fuchsia::ui::policy::Presenter::Name_}},
            .source = ChildRef{kRootPresenter},
            .targets = {ParentRef()}});
  realm_builder->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                                .source = ChildRef{kScenicTestRealm},
                                .targets = {ParentRef()}});
}

// Combines all vectors in `vecs` into one.
template <typename T>
std::vector<T> merge(std::initializer_list<std::vector<T>> vecs) {
  std::vector<T> result;
  for (auto v : vecs) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

// This component implements the test.touch.ResponseListener protocol
// and the interface for a RealmBuilder MockComponent. A MockComponent
// is a component that is implemented here in the test, as opposed to elsewhere
// in the system. When it's inserted to the realm, it will act like a proper
// component. This is accomplished, in part, because the realm_builder
// library creates the necessary plumbing. It creates a manifest for the component
// and routes all capabilities to and from it.
class ResponseListenerServer : public ResponseListener, public LocalComponent {
 public:
  explicit ResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |test::touch::ResponseListener|
  void Respond(test::touch::PointerData pointer_data) override {
    FX_CHECK(respond_callback_) << "Expected callback to be set for test.touch.Respond().";
    respond_callback_(std::move(pointer_data));
  }

  // |MockComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    // When this component starts, add a binding to the test.touch.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(mock_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<test::touch::ResponseListener>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    mock_handles_.emplace_back(std::move(mock_handles));
  }

  void SetRespondCallback(fit::function<void(test::touch::PointerData)> callback) {
    respond_callback_ = std::move(callback);
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<LocalComponentHandles>> mock_handles_;
  fidl::BindingSet<test::touch::ResponseListener> bindings_;
  fit::function<void(test::touch::PointerData)> respond_callback_;
};

class TouchInputBase : public gtest::RealLoopFixture {
 protected:
  TouchInputBase()
      : realm_builder_(std::make_unique<RealmBuilder>(RealmBuilder::Create())), realm_() {}

  ~TouchInputBase() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    BuildRealm(this->GetTestComponents(), this->GetTestRoutes());

    // Get the display dimensions
    scenic_ = realm()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
      display_width_ = display_info.width_in_px;
      display_height_ = display_info.height_in_px;
      FX_LOGS(INFO) << "Got display_width = " << display_width_
                    << " and display_height = " << display_height_;
    });
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });
  }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() { return {}; }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<Route> GetTestRoutes() { return {}; }

  // Launches the test client by connecting to fuchsia.ui.app.ViewProvider protocol.
  // This method should only be invoked if this protocol has been exposed from
  // the root of the test realm. After establishing a connection, this method
  // listens for the client is_rendering signal and calls |on_is_rendering| when it arrives.
  void LaunchClient(std::string debug_name) {
    auto tokens_rt = scenic::ViewTokenPair::New();  // Root Presenter -> Test
    auto tokens_tf = scenic::ViewTokenPair::New();  // Test -> Client

    // Instruct Root Presenter to present test's View.
    auto root_presenter = realm()->Connect<fuchsia::ui::policy::Presenter>();
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
    // Note well. There is a significant difference in how ViewProvider is
    // vended and used, between CF v1 and CF v2. This test follows the CF v2
    // style: the realm specifies a component C that can serve ViewProvider, and
    // when the test runner asks for that protocol, C is launched by Component
    // Manager. In contrast, production uses CF v1 style, where a parent
    // component P launches a child component C directly, and P connects to C's
    // ViewProvider directly. However, this difference does not impact the
    // testing logic.
    auto view_provider = realm()->Connect<fuchsia::ui::app::ViewProvider>();
    view_provider->CreateView(std::move(tokens_tf.view_token.value), /* in */ nullptr,
                              /* out */ nullptr);

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
    response_listener()->SetRespondCallback([expected_x, expected_y, component_name,
                                             &input_injection_time, &injection_complete](
                                                test::touch::PointerData pointer_data) {
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
    });
  }

  // Calls test.touch.TestAppLauncher::Launch.
  // Only works if we've already launched a client that serves test.touch.TestAppLauncher.
  void LaunchEmbeddedClient(std::string debug_name) {
    // Launch the embedded app.
    auto test_app_launcher = realm()->Connect<test::touch::TestAppLauncher>();
    bool child_launched = false;
    test_app_launcher->Launch(debug_name, [&child_launched] { child_launched = true; });
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
    auto parameters = std::make_unique<fuchsia::ui::input::TouchscreenDescriptor>();
    *parameters = {.x = {.range = {.min = -1000, .max = 1000}},
                   .y = {.range = {.min = -1000, .max = 1000}},
                   .max_finger_id = 10};

    // Register it against Root Presenter.
    fuchsia::ui::input::DeviceDescriptor device{.touchscreen = std::move(parameters)};
    auto registry = realm()->Connect<fuchsia::ui::input::InputDeviceRegistry>();
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
      auto touch = std::make_unique<fuchsia::ui::input::TouchscreenReport>();
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
      auto touch = std::make_unique<fuchsia::ui::input::TouchscreenReport>();
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

  RealmBuilder* builder() { return realm_builder_.get(); }
  RealmRoot* realm() { return realm_.get(); }

  ResponseListenerServer* response_listener() { return response_listener_.get(); }

 private:
  void BuildRealm(const std::vector<std::pair<ChildName, LegacyUrl>>& components,
                  const std::vector<Route>& routes) {
    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    response_listener_ = std::make_unique<ResponseListenerServer>(dispatcher());
    builder()->AddLocalChild(kMockResponseListener, response_listener_.get());

    // Add all components shared by each test to the realm.
    AddBaseComponents(builder());

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : components) {
      builder()->AddLegacyChild(name, component);
    }

    // Add the necessary routing for each of the base components added above.
    AddBaseRoutes(builder());

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : routes) {
      builder()->AddRoute(route);
    }

    // Finally, build the realm using the provided components and routes.
    realm_ = std::make_unique<RealmRoot>(builder()->Build());
  }

  template <typename TimeT>
  TimeT RealNow();

  template <>
  zx::time RealNow() {
    return zx::clock::get_monotonic();
  }

  template <>
  time_utc RealNow() {
    zx::unowned_clock utc_clock(zx_utc_reference_get());
    zx_time_t now;
    FX_CHECK(utc_clock->read(&now) == ZX_OK);
    return time_utc(now);
  }

  template <typename TimeT>
  uint64_t TimeToUint(const TimeT& time) {
    FX_CHECK(time.get() >= 0);
    return static_cast<uint64_t>(time.get());
  }

  std::unique_ptr<RealmBuilder> realm_builder_;
  std::unique_ptr<RealmRoot> realm_;

  std::unique_ptr<ResponseListenerServer> response_listener_;

  std::unique_ptr<scenic::Session> session_;

  int injection_count_ = 0;

  fuchsia::ui::scenic::ScenicPtr scenic_;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  // Test view and child view's ViewHolder.
  std::unique_ptr<scenic::ViewHolder> view_holder_;
  std::unique_ptr<scenic::View> view_;

  fuchsia::sys::ComponentControllerPtr client_component_;
  std::shared_ptr<sys::ServiceDirectory> child_services_;
};

class FlutterInputTest : public TouchInputBase {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {
        std::make_pair(kFlutterClient, kFlutterClientUrl),
    };
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({GetFlutterRoutes(ChildRef{kFlutterClient}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kFlutterClient},
                       .targets = {ParentRef()}},
                  }});
  }

  // Routes needed to setup Flutter client.
  static std::vector<Route> GetFlutterRoutes(ChildRef target) {
    return {{.capabilities = {Protocol{test::touch::ResponseListener::Name_}},
             .source = ChildRef{kMockResponseListener},
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
             .source = ChildRef{kScenicTestRealm},
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
             .source = ParentRef(),
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::vulkan::loader::Loader::Name_}},
             .source = ParentRef(),
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
             .source = ParentRef(),
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
             .source = ParentRef(),
             .targets = {target}}};
  }

  static constexpr auto kFlutterClient = "flutter_client";
  static constexpr auto kFlutterClientUrl =
      "fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cmx";
};

TEST_F(FlutterInputTest, FlutterTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient("FlutterTap");

  bool injection_complete = false;
  SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                          input_injection_time,
                          /*component_name=*/"one-flutter", injection_complete);

  input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
  RunLoopUntil([&injection_complete] { return injection_complete; });
}

class GfxInputTest : public TouchInputBase {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {std::make_pair(kCppGfxClient, kCppGfxClientUrl)};
  }

  std::vector<Route> GetTestRoutes() override {
    return {
        {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
         .source = ChildRef{kCppGfxClient},
         .targets = {ParentRef()}},
        {.capabilities = {Protocol{test::touch::ResponseListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {ChildRef{kCppGfxClient}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {ChildRef{kCppGfxClient}}},
        {.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kCppGfxClient}}},
    };
  }

 private:
  static constexpr auto kCppGfxClient = "gfx_client";
  static constexpr auto kCppGfxClientUrl =
      "fuchsia-pkg://fuchsia.com/touch-gfx-client#meta/touch-gfx-client.cmx";
};

TEST_F(GfxInputTest, CppGfxClientTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient("CppGfxClientTap");

  bool injection_complete = false;
  SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                          input_injection_time,
                          /*component_name=*/"touch-gfx-client", injection_complete);

  input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
  RunLoopUntil([&injection_complete] { return injection_complete; });
}

class WebEngineTest : public TouchInputBase {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() {
    return {
        std::make_pair(kOneChromiumClient, kOneChromiumUrl),
        std::make_pair(kFontsProvider, kFontsProviderUrl),
        std::make_pair(kTextManager, kTextManagerUrl),
        std::make_pair(kIntl, kIntlUrl),
        std::make_pair(kMemoryPressureProvider, kMemoryPressureProviderUrl),
        std::make_pair(kNetstack, kNetstackUrl),
        std::make_pair(kWebContextProvider, kWebContextProviderUrl),
        std::make_pair(kSemanticsManager, kSemanticsManagerUrl),
    };
  }

  std::vector<Route> GetTestRoutes() {
    return merge({GetWebEngineRoutes(ChildRef{kOneChromiumClient}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kOneChromiumClient},
                       .targets = {ParentRef()}},
                  }});
  }

  // Injects an input event, and posts a task to retry after `kTapRetryInterval`.
  //
  // We post the retry task because the first input event we send to WebEngine may be lost.
  // The reason the first event may be lost is that there is a race condition as the WebEngine
  // starts up.
  //
  // More specifically: in order for our web app's JavaScript code (see kAppCode in
  // one-chromium.cc)
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
  void TryInject(time_utc* input_injection_time) {
    *input_injection_time = InjectInput<time_utc>(TapLocation::kTopLeft);
    async::PostDelayedTask(
        dispatcher(), [this, input_injection_time] { TryInject(input_injection_time); },
        kTapRetryInterval);
  }

  // Helper method for checking the test.touch.ResponseListener response from a web app.
  void SetResponseExpectationsWeb(float expected_x, float expected_y,
                                  time_utc& input_injection_time, std::string component_name,
                                  bool& injection_complete) {
    response_listener()->SetRespondCallback(
        [expected_x, expected_y, component_name, &injection_complete,
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

          zx::duration elapsed_time = time_utc(pointer_data.time_received()) - input_injection_time;
          EXPECT_NE(elapsed_time.get(), ZX_TIME_INFINITE);
          FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
          FX_LOGS(INFO) << "Chromium Received Time (ns): " << pointer_data.time_received();
          FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();

          // Allow for minor rounding differences in coordinates. As noted above, `device_x` and
          // `device_y` may have an error of up to `device_pixel_ratio` physical pixels.
          EXPECT_NEAR(device_x, expected_x, device_pixel_ratio);
          EXPECT_NEAR(device_y, expected_y, device_pixel_ratio);

          injection_complete = true;
        });
  }

  // Routes needed to setup Chromium client.
  static std::vector<Route> GetWebEngineRoutes(ChildRef target) {
    return {
        {.capabilities = {Protocol{test::touch::ResponseListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::fonts::Provider::Name_}},
         .source = ChildRef{kFontsProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::ui::input::ImeService::Name_}},
         .source = ChildRef{kTextManager},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::ui::input::ImeVisibilityService::Name_}},
         .source = ChildRef{kTextManager},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::intl::PropertyProvider::Name_}},
         .source = ChildRef{kIntl},
         .targets = {target, ChildRef{kSemanticsManager}}},
        {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
         .source = ChildRef{kMemoryPressureProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::netstack::Netstack::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::net::interfaces::State::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_}},
         .source = ChildRef{kSemanticsManager},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
         .source = ChildRef{kWebContextProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kFontsProvider}, ChildRef{kSemanticsManager}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {ChildRef{kSemanticsManager}}},
        {.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::cobalt::LoggerFactory::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}, ChildRef{kOneChromiumClient}}},
        {.capabilities = {Protocol{fuchsia::scheduler::ProfileProvider::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::kernel::RootJobForInspect::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::kernel::Stats::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
    };
  }

  static constexpr auto kOneChromiumClient = "chromium_client";
  static constexpr auto kOneChromiumUrl =
      "fuchsia-pkg://fuchsia.com/one-chromium#meta/one-chromium.cmx";

 private:
  static constexpr auto kFontsProvider = "fonts_provider";
  static constexpr auto kFontsProviderUrl = "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx";

  static constexpr auto kTextManager = "text_manager";
  static constexpr auto kTextManagerUrl =
      "fuchsia-pkg://fuchsia.com/text_manager#meta/text_manager.cmx";

  static constexpr auto kIntl = "intl";
  static constexpr auto kIntlUrl =
      "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cmx";

  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl =
      "fuchsia-pkg://fuchsia.com/memory_monitor#meta/memory_monitor.cmx";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl =
      "fuchsia-pkg://fuchsia.com/touch-input-test#meta/netstack.cmx";

  static constexpr auto kWebContextProvider = "web_context_provider";
  static constexpr auto kWebContextProviderUrl =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";

  static constexpr auto kSemanticsManager = "semantics_manager";
  static constexpr auto kSemanticsManagerUrl =
      "fuchsia-pkg://fuchsia.com/a11y-manager#meta/a11y-manager.cmx";

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
  // Use a UTC time for compatibility with the time reported by `Date.now()` in web-engine.
  time_utc input_injection_time(0);

  // Note well: unlike one-flutter and cpp-gfx-client, the web app may be rendering before
  // it is hittable. Nonetheless, waiting for rendering is better than injecting the touch
  // immediately. In the event that the app is not hittable, `TryInject()` will retry.
  LaunchClient("ChromiumTap");
  client_component().events().OnTerminated = [](int64_t return_code,
                                                fuchsia::sys::TerminationReason reason) {
    // Unlike the Flutter and C++ apps, the process hosting the web app's logic doesn't retain
    // the view token for the life of the app (the process passes that token on to the web
    // engine process). Consequently, we can't just rely on the IsViewDisconnected message to
    // detect early termination of the app.
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

// Tests that rely on Embedding Flutter component. It provides convenience
// static routes that subclass can inherit.
class EmbeddingFlutterTest {
 protected:
  // Components needed for Embedding Flutter to be in realm.
  static std::vector<std::pair<ChildName, LegacyUrl>> GetEmbeddingFlutterComponents() {
    return {
        std::make_pair(kEmbeddingFlutter, kEmbeddingFlutterUrl),
    };
  }

  // Routes needed for Embedding Flutter to run.
  static std::vector<Route> GetEmbeddingFlutterRoutes() {
    return {
        {.capabilities = {Protocol{test::touch::TestAppLauncher::Name_}},
         .source = ChildRef{kEmbeddingFlutter},
         .targets = {ParentRef()}},
        {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
         .source = ChildRef{kEmbeddingFlutter},
         .targets = {ParentRef()}},
        {.capabilities = {Protocol{test::touch::ResponseListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {ChildRef{kEmbeddingFlutter}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ChildRef{kScenicTestRealm},
         .targets = {ChildRef{kEmbeddingFlutter}}},

        // Needed to launch Embedded Client.
        {.capabilities = {Protocol{fuchsia::sys::Environment::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kEmbeddingFlutter}}},
        {.capabilities = {Protocol{fuchsia::sys::Launcher::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kEmbeddingFlutter}}},

        // Needed for Flutter runner.
        {.capabilities = {Protocol{fuchsia::vulkan::loader::Loader::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kEmbeddingFlutter}}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kEmbeddingFlutter}}},
        {.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kEmbeddingFlutter}}},
    };
  }

  static constexpr auto kEmbeddingFlutter = "embedding_flutter";
  static constexpr auto kEmbeddingFlutterUrl =
      "fuchsia-pkg://fuchsia.com/embedding-flutter#meta/embedding-flutter.cmx";
};

class FlutterInFlutterTest : public FlutterInputTest, public EmbeddingFlutterTest {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return merge({EmbeddingFlutterTest::GetEmbeddingFlutterComponents(),
                  FlutterInputTest::GetTestComponents()});
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({EmbeddingFlutterTest::GetEmbeddingFlutterRoutes(),
                  FlutterInputTest::GetFlutterRoutes(ChildRef{kEmbeddingFlutter})});
  }
};

TEST_F(FlutterInFlutterTest, FlutterInFlutterTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  // Launch the embedding app.
  LaunchClient("FlutterInFlutterTap");

  // Launch the embedded app.
  LaunchEmbeddedClient("fuchsia-pkg://fuchsia.com/one-flutter#meta/one-flutter.cmx");

  // Embedded app takes up the left half of the screen. Expect response from it
  // when injecting to the left.
  {
    bool injection_complete = false;
    SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                            /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                            input_injection_time,
                            /*component_name=*/"one-flutter", injection_complete);

    input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
    RunLoopUntil([&injection_complete] { return injection_complete; });
  }

  // Parent app takes up the right half of the screen. Expect response from it
  // when injecting to the right.
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

class WebInFlutterTest : public WebEngineTest, public EmbeddingFlutterTest {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return merge({
        GetEmbeddingFlutterComponents(),
        WebEngineTest::GetTestComponents(),
    });
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({EmbeddingFlutterTest::GetEmbeddingFlutterRoutes(),
                  WebEngineTest::GetWebEngineRoutes(ChildRef{kEmbeddingFlutter})});
  }
};

TEST_F(WebInFlutterTest, WebInFlutterTap) {
  // Launch the embedding app.
  LaunchClient("WebInFlutterTap");

  // Launch the embedded app.
  LaunchEmbeddedClient("fuchsia-pkg://fuchsia.com/one-chromium#meta/one-chromium.cmx");

  // Parent app takes up the right half of the screen. Expect response from it
  // when injecting to the right.
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

  // Embedded app takes up the left half of the screen. Expect response from it
  // when injecting to the left.
  {
    // Use a UTC time for compatibility with the time reported by `Date.now()` in web-engine.
    time_utc input_injection_time(0);

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
