// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
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

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/util/portable_ui_test.h"

// This test exercises the touch input dispatch path from Input Pipeline to a Scenic client. It is a
// multi-component test, and carefully avoids sleeping or polling for component coordination.
// - It runs real Root Presenter, Input Pipeline, and Scenic components.
// - It uses a fake display controller; the physical device is unused.
//
// Components involved
// - This test program
// - Input Pipeline
// - Root Presenter
// - Scenic
// - Child view, a Scenic client
//
// Touch dispatch path
// - Test program's injection -> Input Pipeline -> Scenic -> Child view
//
// Setup sequence
// - The test sets up this view hierarchy:
//   - Top level scene, owned by Root Presenter.
//   - Child view, owned by the ui client.
// - The test waits for a Scenic event that verifies the child has UI content in the scene graph.
// - The test injects input into Input Pipeline, emulating a display's touch report.
// - Input Pipeline dispatches the touch event to Scenic, which in turn dispatches it to the child.
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
//   scenic  input-pipeline
//
// For more information about testing v2 components and realm_builder,
// visit the following links:
//
// Testing: https://fuchsia.dev/fuchsia-src/concepts/testing/v2
// Realm Builder: https://fuchsia.dev/fuchsia-src/development/components/v2/realm_builder

namespace {

using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::ConfigValue;
using component_testing::DirectoryContents;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

// Alias for Component child name as provided to Realm Builder.
using ChildName = std::string;

// Alias for Component Legacy URL as provided to Realm Builder.
using LegacyUrl = std::string;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Maximum distance between two physical pixel coordinates so that they are considered equal.
constexpr float kEpsilon = 0.5f;

constexpr auto kTouchScreenMaxDim = 1000;
constexpr auto kTouchScreenMinDim = -1000;
constexpr auto kMoveEventCount = 5;

// The dimensions of the fake display used in tests. Used in calculating the expected distance
// between any two tap events present in the response to a swipe event.
// Note: These values are currently hard coded in the fake display and should be changed
// accordingly.
// TODO(fxb/111483): Remove the dependency of the tests on these hard coded values.
constexpr auto kDisplayWidth = 1280;
constexpr auto kDisplayHeight = 800;

// The type used to measure UTC time. The integer value here does not matter so
// long as it differs from the ZX_CLOCK_MONOTONIC=0 defined by Zircon.
using time_utc = zx::basic_time<1>;

constexpr auto kMockResponseListener = "response_listener";

struct UIStackConfig {
  bool use_scene_manager = false;
  bool use_flatland = false;
  int32_t display_rotation = 0;
};

std::vector<UIStackConfig> UIStackConfigsToTest() {
  std::vector<UIStackConfig> configs;

  // GFX x RP
  configs.push_back({.use_scene_manager = false, .use_flatland = false, .display_rotation = 90});

  // GFX x SM
  configs.push_back({.use_scene_manager = true, .use_flatland = false, .display_rotation = 90});

  // Flatland X SM
  configs.push_back({.use_scene_manager = true, .use_flatland = true, .display_rotation = 90});

  return configs;
}

template <typename T>
std::vector<std::tuple<T>> AsTuples(std::vector<T> v) {
  std::vector<std::tuple<T>> result;
  for (const auto& elt : v) {
    result.push_back(elt);
  }

  return result;
}

enum class TapLocation { kTopLeft, kTopRight };

enum class SwipeGesture {
  UP = 1,
  DOWN,
  LEFT,
  RIGHT,
};

struct ExpectedSwipeEvent {
  double x = 0, y = 0;
};

struct InjectSwipeParams {
  SwipeGesture direction = SwipeGesture::UP;
  int begin_x = 0, begin_y = 0;
  std::vector<ExpectedSwipeEvent> expected_events;
};

// Combines all vectors in `vecs` into one.
template <typename T>
std::vector<T> merge(std::initializer_list<std::vector<T>> vecs) {
  std::vector<T> result;
  for (auto v : vecs) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

// Checks whether all the coordinates in |expected_events| are contained in |actual_events|.
void AssertSwipeEvents(
    const std::vector<fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest>&
        actual_events,
    const std::vector<ExpectedSwipeEvent>& expected_events) {
  FX_DCHECK(actual_events.size() == expected_events.size());

  for (size_t i = 0; i < actual_events.size(); i++) {
    const auto& actual_x = actual_events[i].local_x();
    const auto& actual_y = actual_events[i].local_y();

    const auto& [expected_x, expected_y] = expected_events[i];

    EXPECT_NEAR(actual_x, expected_x, kEpsilon);
    EXPECT_NEAR(actual_y, expected_y, kEpsilon);
  }
}

InjectSwipeParams GetLeftSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayWidth) / static_cast<double>(kMoveEventCount);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from the
  // right edge to the left edge should appear as a swipe in the middle of the screen from the
  // top edge to the the bottom edge.
  for (double i = 0; i <= static_cast<double>(kMoveEventCount); i++) {
    expected_events.push_back({
        .x = static_cast<double>(kDisplayHeight) / 2,
        .y = i * tap_distance,
    });
  }

  return {.direction = SwipeGesture::LEFT,
          .begin_x = kTouchScreenMaxDim,
          .begin_y = 0,
          .expected_events = std::move(expected_events)};
}

InjectSwipeParams GetRightSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayWidth) / static_cast<double>(kMoveEventCount);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from
  // the left edge to the right edge should appear as a swipe in the middle of the screen from
  // the bottom edge to the top edge.
  for (double i = static_cast<double>(kMoveEventCount); i >= 0; i--) {
    expected_events.push_back({
        .x = static_cast<double>(kDisplayHeight) / 2,
        .y = i * tap_distance,
    });
  }

  return {.direction = SwipeGesture::RIGHT,
          .begin_x = kTouchScreenMinDim,
          .begin_y = 0,
          .expected_events = std::move(expected_events)};
}

InjectSwipeParams GetUpwardSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayHeight) / static_cast<double>(kMoveEventCount);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from the
  // bottom edge to the top edge should appear as a swipe in the middle of the screen from the
  // right edge to the left edge.
  for (double i = static_cast<double>(kMoveEventCount); i >= 0; i--) {
    expected_events.push_back({
        .x = i * tap_distance,
        .y = static_cast<double>(kDisplayWidth) / 2,
    });
  }

  return {.direction = SwipeGesture::UP,
          .begin_x = 0,
          .begin_y = kTouchScreenMaxDim,
          .expected_events = std::move(expected_events)};
}

InjectSwipeParams GetDownwardSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayHeight) / static_cast<double>(kMoveEventCount);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from the
  // top edge to the bottom edge should appear as a swipe in the middle of the screen from the
  // left edge to the right edge.
  for (double i = 0; i <= static_cast<double>(kMoveEventCount); i++) {
    expected_events.push_back({
        .x = i * tap_distance,
        .y = static_cast<double>(kDisplayWidth) / 2,
    });
  }

  return {.direction = SwipeGesture::DOWN,
          .begin_x = 0,
          .begin_y = kTouchScreenMinDim,
          .expected_events = std::move(expected_events)};
}

bool CompareDouble(double f0, double f1, double epsilon) { return std::abs(f0 - f1) <= epsilon; }

// This component implements the test.touch.ResponseListener protocol
// and the interface for a RealmBuilder LocalComponent. A LocalComponent
// is a component that is implemented here in the test, as opposed to elsewhere
// in the system. When it's inserted to the realm, it will act like a proper
// component. This is accomplished, in part, because the realm_builder
// library creates the necessary plumbing. It creates a manifest for the component
// and routes all capabilities to and from it.
class ResponseListenerServer : public fuchsia::ui::test::input::TouchInputListener,
                               public LocalComponent {
 public:
  explicit ResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |fuchsia::ui::test::input::TouchInputListener|
  void ReportTouchInput(
      fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest request) override {
    events_received_.push_back(std::move(request));
  }

  // |LocalComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    // When this component starts, add a binding to the test.touch.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<fuchsia::ui::test::input::TouchInputListener>(
                     [this](auto request) {
                       bindings_.AddBinding(this, std::move(request), dispatcher_);
                     })) == ZX_OK);
    local_handles_.emplace_back(std::move(local_handles));
  }

  const std::vector<fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest>&
  events_received() {
    return events_received_;
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<LocalComponentHandles>> local_handles_;
  fidl::BindingSet<fuchsia::ui::test::input::TouchInputListener> bindings_;
  std::vector<fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest> events_received_;
};

template <typename... Ts>
class TouchInputBase : public ui_testing::PortableUITest,
                       public testing::WithParamInterface<std::tuple<UIStackConfig, Ts...>> {
 protected:
  ~TouchInputBase() override {
    FX_CHECK(touch_injection_request_count() > 0) << "injection expected but didn't happen.";
  }

  std::string GetTestUIStackUrl() override { return "#meta/test-ui-stack.cm"; }

  void SetUp() override {
    ui_testing::PortableUITest::SetUp();

    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    // Get the display dimensions.
    FX_LOGS(INFO) << "Waiting for scenic display info";
    scenic_ = realm_root()->template Connect<fuchsia::ui::scenic::Scenic>();
    scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
      display_width_ = display_info.width_in_px;
      display_height_ = display_info.height_in_px;
      FX_LOGS(INFO) << "Got display_width = " << display_width_
                    << " and display_height = " << display_height_;
    });
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });

    // Register input injection device.
    FX_LOGS(INFO) << "Registering input injection device";
    RegisterTouchScreen();
  }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() { return {}; }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<Route> GetTestRoutes() { return {}; }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, std::string>> GetTestV2Components() { return {}; }

  bool LastEventReceivedMatches(float expected_x, float expected_y, std::string component_name) {
    const auto& events_received = response_listener_->events_received();
    if (events_received.empty()) {
      return false;
    }

    const auto& last_event = events_received.back();

    auto pixel_scale = last_event.has_device_pixel_ratio() ? last_event.device_pixel_ratio() : 1;

    auto actual_x = pixel_scale * last_event.local_x();
    auto actual_y = pixel_scale * last_event.local_y();
    auto actual_component_name = last_event.component_name();

    FX_LOGS(INFO) << "Expecting event for component " << component_name << " at (" << expected_x
                  << ", " << expected_y << ")";
    FX_LOGS(INFO) << "Received event for component " << actual_component_name << " at (" << actual_x
                  << ", " << actual_y << "), accounting for pixel scale of " << pixel_scale;

    return CompareDouble(actual_x, expected_x, pixel_scale) &&
           CompareDouble(actual_y, expected_y, pixel_scale) &&
           actual_component_name == component_name;
  }

  void InjectInput(TapLocation tap_location) {
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
        InjectTap(/* x = */ 500, /* y = */ -500);
        break;
      case TapLocation::kTopRight:
        // center of bottom right quadrant -> ends up as center of top right quadrant
        InjectTap(/* x = */ 500, /* y = */ 500);
        break;
      default:
        FX_NOTREACHED();
    }
  }

  // Inject directly into Input Pipeline, using fuchsia.input.injection FIDLs. A swipe gesture is
  // mimicked by injecting |swipe_length| touch events across the length of the display, with a
  // delay of 10 msec. For the fake display used in this test, and swipe_length=N, this results in
  // events separated by 50 pixels.
  void InjectEdgeToEdgeSwipe(SwipeGesture direction, int begin_x, int begin_y) {
    int x_dir = 0, y_dir = 0;
    switch (direction) {
      case SwipeGesture::UP:
        y_dir = -1;
        break;
      case SwipeGesture::DOWN:
        y_dir = 1;
        break;
      case SwipeGesture::LEFT:
        x_dir = -1;
        break;
      case SwipeGesture::RIGHT:
        x_dir = 1;
        break;
      default:
        FX_NOTREACHED();
    }

    auto touchscreen_width = kTouchScreenMaxDim - kTouchScreenMinDim;
    auto touchscreen_height = kTouchScreenMaxDim - kTouchScreenMinDim;

    fuchsia::ui::test::input::TouchScreenSimulateSwipeRequest swipe_request;
    swipe_request.mutable_start_location()->x = begin_x;
    swipe_request.mutable_start_location()->y = begin_y;
    swipe_request.mutable_end_location()->x = begin_x + x_dir * touchscreen_width;
    swipe_request.mutable_end_location()->y = begin_y + y_dir * touchscreen_height;
    // Generate move events 50 pixels apart.
    swipe_request.set_move_event_count(kMoveEventCount);

    InjectSwipe(/* start_x = */ begin_x, /* start_y = */ begin_y,
                /* end_x = */ begin_x + x_dir * touchscreen_width,
                /* end_y = */ begin_y + y_dir * touchscreen_height,
                /* move_event_count = */ kMoveEventCount);
  }

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return display_width_; }
  uint32_t display_height() const { return display_height_; }

  fuchsia::sys::ComponentControllerPtr& client_component() { return client_component_; }

  ResponseListenerServer* response_listener() { return response_listener_.get(); }

 private:
  void ExtendRealm() override {
    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    response_listener_ = std::make_unique<ResponseListenerServer>(dispatcher());
    realm_builder()->AddLocalChild(kMockResponseListener, response_listener_.get());

    realm_builder()->AddRoute({.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                               .source = kTestUIStackRef,
                               .targets = {ParentRef()}});

    // Use a display rotation of 90 degrees.
    auto ui_stack_config = std::get<0>(this->GetParam());
    realm_builder()->InitMutableConfigToEmpty(kTestUIStack);
    realm_builder()->SetConfigValue(kTestUIStack, "use_scene_manager",
                                    ConfigValue::Bool(ui_stack_config.use_scene_manager));
    realm_builder()->SetConfigValue(kTestUIStack, "use_flatland",
                                    ConfigValue::Bool(ui_stack_config.use_flatland));
    realm_builder()->SetConfigValue(kTestUIStack, "display_rotation",
                                    ConfigValue::Uint32(ui_stack_config.display_rotation));

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : GetTestComponents()) {
      realm_builder()->AddLegacyChild(name, component);
    }

    for (const auto& [name, component] : GetTestV2Components()) {
      realm_builder()->AddChild(name, component);
    }

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : GetTestRoutes()) {
      realm_builder()->AddRoute(route);
    }
  }

  std::unique_ptr<ResponseListenerServer> response_listener_;

  fuchsia::ui::scenic::ScenicPtr scenic_;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  fuchsia::sys::ComponentControllerPtr client_component_;
};

template <typename... Ts>
class FlutterInputTestBase : public TouchInputBase<Ts...> {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
    return {
        std::make_pair(kFlutterRealm, kFlutterRealmUrl),
    };
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({GetFlutterRoutes(ChildRef{kFlutterRealm}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kFlutterRealm},
                       .targets = {ParentRef()}},
                  }});
  }

  // Routes needed to setup Flutter client.
  static std::vector<Route> GetFlutterRoutes(ChildRef target) {
    return {{.capabilities = {Protocol{fuchsia::ui::test::input::TouchInputListener::Name_}},
             .source = ChildRef{kMockResponseListener},
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                              Protocol{fuchsia::sysmem::Allocator::Name_},
                              Protocol{fuchsia::tracing::provider::Registry::Name_},
                              Protocol{fuchsia::vulkan::loader::Loader::Name_}},
             .source = ParentRef(),
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
             .source = ui_testing::PortableUITest::kTestUIStackRef,
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::ui::composition::Flatland::Name_},
                              Protocol{fuchsia::ui::composition::Allocator::Name_}},
             .source = ui_testing::PortableUITest::kTestUIStackRef,
             .targets = {target}}};
  }

  static constexpr auto kFlutterRealm = "flutter_realm";
  static constexpr auto kFlutterRealmUrl = "#meta/one-flutter-realm.cm";
};

class FlutterInputTest : public FlutterInputTestBase<> {};

INSTANTIATE_TEST_SUITE_P(FlutterInputTestParameterized, FlutterInputTest,
                         testing::ValuesIn(AsTuples(UIStackConfigsToTest())));

TEST_P(FlutterInputTest, FlutterTap) {
  // Launch client view, and wait until it's rendering to proceed with the test.
  FX_LOGS(INFO) << "Initializing scene";
  LaunchClient();
  FX_LOGS(INFO) << "Client launched";

  InjectInput(TapLocation::kTopLeft);
  RunLoopUntil([this] {
    return LastEventReceivedMatches(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                                    /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                                    "one-flutter");
  });
}

class FlutterSwipeTest : public FlutterInputTestBase<InjectSwipeParams> {};

INSTANTIATE_TEST_SUITE_P(
    FlutterSwipeTestParameterized, FlutterSwipeTest,
    testing::Combine(testing::ValuesIn(UIStackConfigsToTest()),
                     testing::Values(GetRightSwipeParams(), GetDownwardSwipeParams(),
                                     GetLeftSwipeParams(), GetUpwardSwipeParams())));

TEST_P(FlutterSwipeTest, SwipeTest) {
  // Launch client view, and wait until it's rendering to proceed with the test.
  FX_LOGS(INFO) << "Initializing scene";
  LaunchClient();
  FX_LOGS(INFO) << "Client launched";

  const auto& [direction, begin_x, begin_y, expected_events] = std::get<1>(GetParam());

  // Inject a swipe on the display. As the child view is rotated by 90 degrees, the direction of the
  // swipe also gets rotated by 90 degrees.
  InjectEdgeToEdgeSwipe(direction, begin_x, begin_y);

  //  Client sends a response for 1 Down and |swipe_length| Move PointerEventPhase events.
  RunLoopUntil([this] {
    return response_listener()->events_received().size() >=
           static_cast<uint32_t>(kMoveEventCount + 1);
  });

  ASSERT_EQ(response_listener()->events_received().size(), expected_events.size());
  AssertSwipeEvents(response_listener()->events_received(), expected_events);
}

template <typename... Ts>
class CppInputTestIpBase : public TouchInputBase<Ts...> {
 protected:
  virtual std::string_view GetViewProvider() = 0;

  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
    return {std::make_pair(kCppGfxClient, kCppGfxClientUrl),
            std::make_pair(kCppFlatlandClient, kCppFlatlandClientUrl)};
  }

  std::vector<Route> GetTestRoutes() override {
    const std::string_view view_provider = GetViewProvider();
    return {
        {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
         .source = ChildRef{view_provider},
         .targets = {ParentRef()}},
        {.capabilities = {Protocol{fuchsia::ui::test::input::TouchInputListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {ChildRef{kCppGfxClient}, ChildRef{kCppFlatlandClient}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ui_testing::PortableUITest::kTestUIStackRef,
         .targets = {ChildRef{kCppGfxClient}}},
        {.capabilities = {Protocol{fuchsia::ui::composition::Flatland::Name_},
                          Protocol{fuchsia::ui::composition::Allocator::Name_}},
         .source = ui_testing::PortableUITest::kTestUIStackRef,
         .targets = {ChildRef{kCppFlatlandClient}}},
    };
  }

  static constexpr auto kCppGfxClient = "touch-gfx-client";
  static constexpr auto kCppFlatlandClient = "touch-flatland-client";

 private:
  static constexpr auto kCppGfxClientUrl = "#meta/touch-gfx-client.cm";
  static constexpr auto kCppFlatlandClientUrl = "#meta/touch-flatland-client.cm";
};

class CppInputTestIp : public CppInputTestIpBase<> {
 protected:
  std::string_view GetViewProvider() override {
    auto ui_stack_config = std::get<0>(GetParam());
    std::string_view view_provider =
        ui_stack_config.use_flatland ? kCppFlatlandClient : kCppGfxClient;
    return view_provider;
  }
};
INSTANTIATE_TEST_SUITE_P(CppInputTestIpParametized, CppInputTestIp,
                         testing::ValuesIn(AsTuples(UIStackConfigsToTest())));

TEST_P(CppInputTestIp, CppClientTap) {
  // Launch client view, and wait until it's rendering to proceed with the test.
  FX_LOGS(INFO) << "Initializing scene";
  LaunchClient();
  FX_LOGS(INFO) << "Client launched";

  InjectInput(TapLocation::kTopLeft);
  RunLoopUntil([this] {
    return LastEventReceivedMatches(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                                    /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                                    static_cast<std::string>(GetViewProvider()));
  });
}

class CppSwipeTest : public CppInputTestIpBase<InjectSwipeParams> {
 protected:
  std::string_view GetViewProvider() override {
    auto ui_stack_config = std::get<0>(GetParam());
    std::string_view view_provider =
        ui_stack_config.use_flatland ? kCppFlatlandClient : kCppGfxClient;
    return view_provider;
  }
};

INSTANTIATE_TEST_SUITE_P(
    CppSwipeTestParameterized, CppSwipeTest,
    testing::Combine(testing::ValuesIn(UIStackConfigsToTest()),
                     testing::Values(GetRightSwipeParams(), GetDownwardSwipeParams(),
                                     GetLeftSwipeParams(), GetUpwardSwipeParams())));

TEST_P(CppSwipeTest, CppClientSwipeTest) {
  // Launch client view, and wait until it's rendering to proceed with the test.
  FX_LOGS(INFO) << "Initializing scene";
  LaunchClient();
  FX_LOGS(INFO) << "Client launched";

  const auto& [direction, begin_x, begin_y, expected_events] = std::get<1>(GetParam());

  // Inject a swipe on the display. As the child view is rotated by 90 degrees, the direction of
  // the swipe also gets rotated by 90 degrees.
  InjectEdgeToEdgeSwipe(direction, begin_x, begin_y);

  //  Client sends a response for 1 Down and |swipe_length| Move PointerEventPhase events.
  RunLoopUntil([this] {
    FX_LOGS(INFO) << "Events received = " << response_listener()->events_received().size();
    FX_LOGS(INFO) << "Events expected = " << kMoveEventCount + 1;
    return response_listener()->events_received().size() >=
           static_cast<uint32_t>(kMoveEventCount + 1);
  });

  const auto& actual_events = response_listener()->events_received();
  ASSERT_EQ(actual_events.size(), expected_events.size());
  AssertSwipeEvents(actual_events, expected_events);
}

class WebEngineTestIp : public TouchInputBase<> {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {
        std::make_pair(kWebContextProvider, kWebContextProviderUrl),
    };
  }

  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
    return {
        std::make_pair(kBuildInfoProvider, kBuildInfoProviderUrl),
        std::make_pair(kFontsProvider, kFontsProviderUrl),
        std::make_pair(kIntl, kIntlUrl),
        std::make_pair(kMemoryPressureProvider, kMemoryPressureProviderUrl),
        std::make_pair(kMockCobalt, kMockCobaltUrl),
        std::make_pair(kNetstack, kNetstackUrl),
        std::make_pair(kOneChromiumClient, kOneChromiumUrl),
        std::make_pair(kTextManager, kTextManagerUrl),
    };
  }

  std::vector<Route> GetTestRoutes() override {
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
  // one-chromium.cc) to receive the injected input, two things must be true before we inject
  // the input:
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
  // For more details, see fxbug.dev/57268.
  //
  // TODO(fxbug.dev/58322): Improve synchronization when we move to Flatland.
  void TryInject() {
    InjectInput(TapLocation::kTopLeft);
    async::PostDelayedTask(
        dispatcher(), [this] { TryInject(); }, kTapRetryInterval);
  }

  // Routes needed to setup Chromium client.
  static std::vector<Route> GetWebEngineRoutes(ChildRef target) {
    return {
        {.capabilities = {Protocol{fuchsia::ui::test::input::TouchInputListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::fonts::Provider::Name_}},
         .source = ChildRef{kFontsProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::ui::input::ImeService::Name_}},
         .source = ChildRef{kTextManager},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
         .source = ChildRef{kMemoryPressureProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::net::interfaces::State::Name_},
                          Protocol{fuchsia::netstack::Netstack::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_},
                          Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = kTestUIStackRef,
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::ui::composition::Flatland::Name_},
                          Protocol{fuchsia::ui::composition::Allocator::Name_}},
         .source = kTestUIStackRef,
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
         .source = ChildRef{kWebContextProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kFontsProvider}}},
        {.capabilities = {Protocol{fuchsia::metrics::MetricEventLoggerFactory::Name_}},
         .source = ChildRef{kMockCobalt},
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}, ChildRef{kOneChromiumClient}}},
        {.capabilities = {Protocol{fuchsia::kernel::RootJobForInspect::Name_},
                          Protocol{fuchsia::kernel::Stats::Name_},
                          Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                          Protocol{fuchsia::tracing::provider::Registry::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::buildinfo::Provider::Name_}},
         .source = ChildRef{kBuildInfoProvider},
         .targets = {target, ChildRef{kWebContextProvider}}},
        {.capabilities = {Protocol{fuchsia::intl::PropertyProvider::Name_}},
         .source = ChildRef{kIntl},
         .targets = {target}},
    };
  }

  static constexpr auto kOneChromiumClient = "chromium_client";
  static constexpr auto kOneChromiumUrl = "#meta/one-chromium.cm";

 private:
  static constexpr auto kFontsProvider = "fonts_provider";
  static constexpr auto kFontsProviderUrl = "#meta/fonts.cm";

  static constexpr auto kTextManager = "text_manager";
  static constexpr auto kTextManagerUrl = "#meta/text_manager.cm";

  static constexpr auto kIntl = "intl";
  static constexpr auto kIntlUrl = "#meta/intl_property_manager.cm";

  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl = "#meta/netstack.cm";

  static constexpr auto kWebContextProvider = "web_context_provider";
  static constexpr auto kWebContextProviderUrl =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";

  static constexpr auto kBuildInfoProvider = "build_info_provider";
  static constexpr auto kBuildInfoProviderUrl = "#meta/fake_build_info.cm";

  static constexpr auto kMockCobalt = "cobalt";
  static constexpr auto kMockCobaltUrl = "#meta/mock_cobalt.cm";

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

INSTANTIATE_TEST_SUITE_P(WebEngineTestIpParameterized, WebEngineTestIp,
                         testing::ValuesIn(AsTuples(UIStackConfigsToTest())));

TEST_P(WebEngineTestIp, ChromiumTap) {
  // Launch client view, and wait until it's rendering to proceed with the test.
  FX_LOGS(INFO) << "Initializing scene";
  LaunchClient();
  FX_LOGS(INFO) << "Client launched";

  // Note well: unlike one-flutter and cpp-gfx-client, the web app may be rendering before
  // it is hittable. Nonetheless, waiting for rendering is better than injecting the touch
  // immediately. In the event that the app is not hittable, `TryInject()` will retry.
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

  TryInject();
  RunLoopUntil([this] {
    return LastEventReceivedMatches(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                                    /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                                    /*component_name=*/"one-chromium");
  });
}

// Tests that rely on Embedding Flutter component. It provides convenience
// static routes that subclass can inherit.
class EmbeddingFlutterTestIp {
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
        {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
         .source = ChildRef{kEmbeddingFlutter},
         .targets = {ParentRef()}},
        {.capabilities = {Protocol{fuchsia::ui::test::input::TouchInputListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {ChildRef{kEmbeddingFlutter}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ui_testing::PortableUITest::kTestUIStackRef,
         .targets = {ChildRef{kEmbeddingFlutter}}},
        {.capabilities = {Protocol{fuchsia::ui::composition::Flatland::Name_},
                          Protocol{fuchsia::ui::composition::Allocator::Name_}},
         .source = ui_testing::PortableUITest::kTestUIStackRef,
         .targets = {ChildRef{kEmbeddingFlutter}}},
        // Needed for Flutter runner.
        {.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                          Protocol{fuchsia::sysmem::Allocator::Name_},
                          Protocol{fuchsia::tracing::provider::Registry::Name_},
                          Protocol{fuchsia::vulkan::loader::Loader::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kEmbeddingFlutter}}},
    };
  }

  static constexpr auto kEmbeddingFlutter = "embedding_flutter";
  static constexpr auto kEmbeddingFlutterUrl = "#meta/embedding-flutter-realm.cm";
};

class FlutterInFlutterTestIp : public FlutterInputTest, public EmbeddingFlutterTestIp {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
    return merge({EmbeddingFlutterTestIp::GetEmbeddingFlutterComponents(),
                  FlutterInputTest::GetTestV2Components()});
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({EmbeddingFlutterTestIp::GetEmbeddingFlutterRoutes(),
                  FlutterInputTest::GetFlutterRoutes(ChildRef{kEmbeddingFlutter}),
                  FlutterInputTest::GetFlutterRoutes(ChildRef{kFlutterRealm}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kFlutterRealm},
                       .targets = {ChildRef{kEmbeddingFlutter}}},
                  }});
  }
};

INSTANTIATE_TEST_SUITE_P(FlutterInFlutterTestIpParameterized, FlutterInFlutterTestIp,
                         testing::ValuesIn(AsTuples(UIStackConfigsToTest())));

TEST_P(FlutterInFlutterTestIp, FlutterInFlutterTap) {
  // Launch client view, and wait until it's rendering to proceed with the test.
  FX_LOGS(INFO) << "Initializing scene";
  LaunchClientWithEmbeddedView();
  FX_LOGS(INFO) << "Client launched";

  // Embedded app takes up the left half of the screen. Expect response from it when injecting to
  // the left.
  {
    InjectInput(TapLocation::kTopLeft);
    RunLoopUntil([this] {
      return LastEventReceivedMatches(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                                      /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                                      /*component_name=*/"one-flutter");
    });
  }

  // Parent app takes up the right half of the screen. Expect response from it when injecting to
  // the right.
  {
    InjectInput(TapLocation::kTopRight);
    RunLoopUntil([this] {
      return LastEventReceivedMatches(
          /*expected_x=*/static_cast<float>(display_height()) * (3.f / 4.f),
          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
          /*component_name=*/"embedding-flutter");
    });
  }
}

class WebInFlutterTestIp : public WebEngineTestIp, public EmbeddingFlutterTestIp {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return WebEngineTestIp::GetTestComponents();
  }

  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
    return merge({
        GetEmbeddingFlutterComponents(),
        WebEngineTestIp::GetTestV2Components(),
    });
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({EmbeddingFlutterTestIp::GetEmbeddingFlutterRoutes(),
                  WebEngineTestIp::GetWebEngineRoutes(ChildRef{kEmbeddingFlutter}),
                  WebEngineTestIp::GetWebEngineRoutes(ChildRef{kOneChromiumClient}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kOneChromiumClient},
                       .targets = {ChildRef{kEmbeddingFlutter}}},
                  }});
  }
};

INSTANTIATE_TEST_SUITE_P(WebInFlutterTestIpParameterized, WebInFlutterTestIp,
                         testing::ValuesIn(AsTuples(UIStackConfigsToTest())));

TEST_P(WebInFlutterTestIp, WebInFlutterTap) {
  // Launch client view, and wait until it's rendering to proceed with the test.
  FX_LOGS(INFO) << "Initializing scene";
  LaunchClientWithEmbeddedView();
  FX_LOGS(INFO) << "Client launched";

  // Parent app takes up the right half of the screen. Expect response from it when injecting to
  // the right.
  {
    InjectInput(TapLocation::kTopRight);
    RunLoopUntil([this] {
      return LastEventReceivedMatches(
          /*expected_x=*/static_cast<float>(display_height()) * (3.f / 4.f),
          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
          /*component_name=*/"embedding-flutter");
    });
  }

  // Embedded app takes up the left half of the screen. Expect response from it when injecting to
  // the left.
  {
    TryInject();
    RunLoopUntil([this] {
      return LastEventReceivedMatches(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                                      /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                                      /*component_name=*/"one-chromium");
    });
  }
}

}  // namespace
