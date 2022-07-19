// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/input/injection/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
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
#include <test/touch/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/input/testing/fake_input_report_device/fake.h"
#include "src/ui/input/testing/fake_input_report_device/reports_reader.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

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
//   touch-input-test-ip.cml (this component)
//
// With the usage of the realm_builder library, we construct a realm during runtime
// and then extend the topology to look like:
//
//    test_manager
//         |
//   touch-input-test-ip.cml (this component)
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

using test::touch::ResponseListener;
using ScenicEvent = fuchsia::ui::scenic::Event;
using GfxEvent = fuchsia::ui::gfx::Event;

// Types imported for the realm_builder library.
using component_testing::ChildRef;
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

// Maximum distance between two view coordinates so that they are considered equal.
constexpr auto kViewCoordinateEpsilon = 0.01;

constexpr auto kTouchScreenMaxDim = 1000;
constexpr auto kTouchScreenMinDim = -1000;

constexpr auto kSwipeLength = 5;

// The dimensions of the fake display used in tests. Used in calculating the expected distance
// between any two tap events present in the response to a swipe event.
// Note: These values are currently hard coded in the fake display and should be changed
// accordingly.
constexpr auto kDisplayWidth = 1024;
constexpr auto kDisplayHeight = 600;

// The type used to measure UTC time. The integer value here does not matter so
// long as it differs from the ZX_CLOCK_MONOTONIC=0 defined by Zircon.
using time_utc = zx::basic_time<1>;

constexpr auto kMockResponseListener = "response_listener";

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
  int swipe_length = 0, begin_x = 0, begin_y = 0;
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
void AssertSwipeEvents(const std::vector<test::touch::PointerData>& actual_events,
                       const std::vector<ExpectedSwipeEvent>& expected_events,
                       const std::vector<zx::time>& expected_injection_times) {
  FX_DCHECK(actual_events.size() == expected_events.size() &&
            actual_events.size() == expected_injection_times.size());

  for (size_t i = 0; i < actual_events.size(); i++) {
    const auto& actual_x = actual_events[i].local_x();
    const auto& actual_y = actual_events[i].local_y();
    const auto& actual_time_received = actual_events[i].time_received();

    const auto& [expected_x, expected_y] = expected_events[i];

    auto elapsed_time =
        zx::basic_time<ZX_CLOCK_MONOTONIC>(actual_time_received) - expected_injection_times[i];

    EXPECT_NEAR(actual_x, expected_x, kEpsilon);
    EXPECT_NEAR(actual_y, expected_y, kEpsilon);
    EXPECT_TRUE(elapsed_time.get() > 0 && elapsed_time.get() != ZX_TIME_INFINITE);
  }
}

InjectSwipeParams GetLeftSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayWidth) / static_cast<double>(kSwipeLength);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from the
  // right edge to the left edge should appear as a swipe in the middle of the screen from the
  // top edge to the the bottom edge.
  for (double i = 0; i <= static_cast<double>(kSwipeLength); i++) {
    expected_events.push_back({
        .x = static_cast<double>(kDisplayHeight) / 2,
        .y = i * tap_distance,
    });
  }

  return {.direction = SwipeGesture::LEFT,
          .swipe_length = kSwipeLength,
          .begin_x = kTouchScreenMaxDim,
          .begin_y = 0,
          .expected_events = std::move(expected_events)};
}

InjectSwipeParams GetRightSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayWidth) / static_cast<double>(kSwipeLength);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from
  // the left edge to the right edge should appear as a swipe in the middle of the screen from
  // the bottom edge to the top edge.
  for (double i = static_cast<double>(kSwipeLength); i >= 0; i--) {
    expected_events.push_back({
        .x = static_cast<double>(kDisplayHeight) / 2,
        .y = i * tap_distance,
    });
  }

  return {.direction = SwipeGesture::RIGHT,
          .swipe_length = kSwipeLength,
          .begin_x = kTouchScreenMinDim,
          .begin_y = 0,
          .expected_events = std::move(expected_events)};
}

InjectSwipeParams GetUpwardSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayHeight) / static_cast<double>(kSwipeLength);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from the
  // bottom edge to the top edge should appear as a swipe in the middle of the screen from the
  // right edge to the left edge.
  for (double i = static_cast<double>(kSwipeLength); i >= 0; i--) {
    expected_events.push_back({
        .x = i * tap_distance,
        .y = static_cast<double>(kDisplayWidth) / 2,
    });
  }

  return {.direction = SwipeGesture::UP,
          .swipe_length = kSwipeLength,
          .begin_x = 0,
          .begin_y = kTouchScreenMaxDim,
          .expected_events = std::move(expected_events)};
}

InjectSwipeParams GetDownwardSwipeParams() {
  std::vector<ExpectedSwipeEvent> expected_events;
  auto tap_distance = static_cast<double>(kDisplayHeight) / static_cast<double>(kSwipeLength);

  // As the child view is rotated by 90 degrees, a swipe in the middle of the display from the
  // top edge to the bottom edge should appear as a swipe in the middle of the screen from the
  // left edge to the right edge.
  for (double i = 0; i <= static_cast<double>(kSwipeLength); i++) {
    expected_events.push_back({
        .x = i * tap_distance,
        .y = static_cast<double>(kDisplayWidth) / 2,
    });
  }

  return {.direction = SwipeGesture::DOWN,
          .swipe_length = kSwipeLength,
          .begin_x = 0,
          .begin_y = kTouchScreenMinDim,
          .expected_events = std::move(expected_events)};
}

// This component implements the test.touch.ResponseListener protocol
// and the interface for a RealmBuilder LocalComponent. A LocalComponent
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

  // |LocalComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> local_handles) override {
    // When this component starts, add a binding to the test.touch.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(local_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<test::touch::ResponseListener>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    local_handles_.emplace_back(std::move(local_handles));
  }

  void SetRespondCallback(fit::function<void(test::touch::PointerData)> callback) {
    respond_callback_ = std::move(callback);
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::vector<std::unique_ptr<LocalComponentHandles>> local_handles_;
  fidl::BindingSet<test::touch::ResponseListener> bindings_;
  fit::function<void(test::touch::PointerData)> respond_callback_;
};

template <typename... Ts>
class TouchInputBase : public gtest::RealLoopFixture,
                       public testing::WithParamInterface<
                           std::tuple<ui_testing::UITestRealm::SceneOwnerType, Ts...>> {
 protected:
  ~TouchInputBase() override {
    FX_CHECK(injection_count_ > 0) << "injection expected but didn't happen.";
  }

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    ui_testing::UITestRealm::Config config;
    config.scene_owner = std::get<0>(this->GetParam());
    config.display_rotation = 90;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
    config.exposed_client_services = {test::touch::TestAppLauncher::Name_};
    config.ui_to_client_services = {fuchsia::ui::scenic::Scenic::Name_,
                                    fuchsia::accessibility::semantics::SemanticsManager::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    // Assemble realm.
    BuildRealm(this->GetTestComponents(), this->GetTestRoutes(), this->GetTestV2Components());

    // Get the display dimensions.
    FX_LOGS(INFO) << "Waiting for scenic display info";
    scenic_ = realm_exposed_services()->template Connect<fuchsia::ui::scenic::Scenic>();
    scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
      display_width_ = display_info.width_in_px;
      display_height_ = display_info.height_in_px;
      FX_LOGS(INFO) << "Got display_width = " << display_width_
                    << " and display_height = " << display_height_;
    });
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });

    // Register input injection device.
    FX_LOGS(INFO) << "Registering input injection device";
    RegisterInjectionDevice();

    // Launch client view, and wait until it's rendering to proceed with the test.
    FX_LOGS(INFO) << "Initializing scene";
    ui_test_manager_->InitializeScene();
    FX_LOGS(INFO) << "Waiting for client view to render";
    RunLoopUntil([this]() { return ui_test_manager_->ClientViewIsRendering(); });
    FX_LOGS(INFO) << "Client view has rendered";
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

  // Calls test.touch.TestAppLauncher::Launch.
  // Only works if we've already launched a client that serves test.touch.TestAppLauncher.
  void LaunchEmbeddedClient(std::string debug_name) {
    // Set up an empty session, only used for synchronization in this method.
    auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get());
    session_ = std::make_unique<scenic::Session>(std::move(session_pair.first),
                                                 std::move(session_pair.second));
    session_->SetDebugName("empty-session-for-synchronization");

    // Launch the embedded app.
    auto test_app_launcher =
        realm_exposed_services()->template Connect<test::touch::TestAppLauncher>();
    bool child_launched = false;
    test_app_launcher->Launch(std::move(debug_name), [&child_launched] { child_launched = true; });
    RunLoopUntil([&child_launched] { return child_launched; });

    // Waits an extra frame to avoid any flakes from the child launching signal firing slightly
    // early.
    bool frame_presented = false;
    session_->set_on_frame_presented_handler([&frame_presented](auto) { frame_presented = true; });
    session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
    RunLoopUntil([&frame_presented] { return frame_presented; });
    session_->set_on_frame_presented_handler([](auto) {});
  }

  // Helper method for checking the test.touch.ResponseListener response from the client app.
  void SetResponseExpectations(float expected_x, float expected_y,
                               zx::basic_time<ZX_CLOCK_MONOTONIC>& input_injection_time,
                               std::string component_name, bool& injection_complete) {
    response_listener()->SetRespondCallback([expected_x, expected_y, component_name,
                                             &input_injection_time, &injection_complete](
                                                test::touch::PointerData pointer_data) {
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
      EXPECT_NEAR(pointer_data.local_x(), expected_x, kViewCoordinateEpsilon);
      EXPECT_NEAR(pointer_data.local_y(), expected_y, kViewCoordinateEpsilon);
      EXPECT_EQ(pointer_data.component_name(), component_name);

      injection_complete = true;
    });
  }

  void RegisterInjectionDevice() {
    registry_ = realm_exposed_services()
                    ->template Connect<fuchsia::input::injection::InputDeviceRegistry>();
    registry_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "Input device registry error: " << zx_status_get_string(status);
    });
    // Create a FakeInputDevice
    fake_input_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>(
        input_device_ptr_.NewRequest(), dispatcher());

    // Set descriptor
    auto device_descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
    auto touch = device_descriptor->mutable_touch()->mutable_input();
    touch->set_touch_type(fuchsia::input::report::TouchType::TOUCHSCREEN);
    touch->set_max_contacts(10);

    fuchsia::input::report::Axis axis;
    axis.unit.type = fuchsia::input::report::UnitType::NONE;
    axis.unit.exponent = 0;
    axis.range.min = kTouchScreenMinDim;
    axis.range.max = kTouchScreenMaxDim;

    fuchsia::input::report::ContactInputDescriptor contact;
    contact.set_position_x(axis);
    contact.set_position_y(axis);
    contact.set_pressure(axis);

    touch->mutable_contacts()->push_back(std::move(contact));

    fake_input_device_->SetDescriptor(std::move(device_descriptor));

    // Register the FakeInputDevice
    registry_->Register(std::move(input_device_ptr_));
    FX_LOGS(INFO) << "Registered touchscreen with x touch range = (-1000, 1000) "
                  << "and y touch range = (-1000, 1000).";
  }

  // Inject directly into Input Pipeline, using fuchsia.input.injection FIDLs.
  template <typename TimeT>
  TimeT InjectInput(TapLocation tap_location) {
    // Set InputReports to inject. One contact at the center of the top right quadrant, followed
    // by no contacts.
    fuchsia::input::report::ContactInputReport contact_input_report;
    contact_input_report.set_contact_id(1);
    contact_input_report.set_position_x(500);
    contact_input_report.set_position_y(-500);

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
        contact_input_report.set_position_x(500);
        contact_input_report.set_position_y(-500);
        break;
      case TapLocation::kTopRight:
        // center of bottom right quadrant -> ends up as center of top right quadrant
        contact_input_report.set_position_x(500);
        contact_input_report.set_position_y(500);
        break;
      default:
        FX_NOTREACHED();
    }

    fuchsia::input::report::TouchInputReport touch_input_report;
    auto contacts = touch_input_report.mutable_contacts();
    contacts->push_back(std::move(contact_input_report));

    fuchsia::input::report::InputReport input_report;
    input_report.set_touch(std::move(touch_input_report));

    std::vector<fuchsia::input::report::InputReport> input_reports;
    input_reports.push_back(std::move(input_report));

    fuchsia::input::report::TouchInputReport remove_touch_input_report;
    fuchsia::input::report::InputReport remove_input_report;
    remove_input_report.set_touch(std::move(remove_touch_input_report));
    input_reports.push_back(std::move(remove_input_report));
    fake_input_device_->SetReports(std::move(input_reports));

    ++injection_count_;
    FX_LOGS(INFO) << "*** Tap injected, count: " << injection_count_;
    return RealNow<TimeT>();
  }

  // Inject directly into Input Pipeline, using fuchsia.input.injection FIDLs. A swipe gesture is
  // mimicked by injecting |swipe_length| touch events across the length of the display, with a
  // delay of 10 msec. For the fake display used in this test, and swipe_length=N, this results in
  // events separated by 50 pixels.
  template <typename TimeT>
  std::vector<TimeT> InjectSwipe(SwipeGesture direction, int swipe_length, int begin_x,
                                 int begin_y) {
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

    std::vector<TimeT> injection_times;

    // Inject a series of input reports.
    //
    // The /config/data/display_rotation (90) specifies how many degrees to rotate the
    // presentation child view, counter-clockwise, in a right-handed coordinate system. Thus,
    // the user observes the child view to rotate *clockwise* by that amount (90).
    //
    // Hence an upward swipe on the display is observed by the child view as a left swipe in its
    // coordinate system.
    std::vector<fuchsia::input::report::InputReport> input_reports;
    auto touchscreen_width = kTouchScreenMaxDim - kTouchScreenMinDim;

    // Note: The display start and end edges must be included.
    for (auto i = 0; i <= swipe_length; i++) {
      auto x = begin_x + x_dir * (touchscreen_width / swipe_length) * i;
      auto y = begin_y + y_dir * (touchscreen_width / swipe_length) * i;

      fuchsia::input::report::ContactInputReport contact_input_report;
      contact_input_report.set_contact_id(1);
      contact_input_report.set_position_x(x);
      contact_input_report.set_position_y(y);

      fuchsia::input::report::TouchInputReport touch_input_report;
      auto contacts = touch_input_report.mutable_contacts();
      contacts->push_back(std::move(contact_input_report));

      fuchsia::input::report::InputReport input_report;
      input_report.set_touch(std::move(touch_input_report));

      input_reports.push_back(std::move(input_report));
      injection_times.push_back(RealNow<TimeT>());

      zx::nanosleep(zx::deadline_after(zx::msec(10)));
      injection_count_++;
    }

    fuchsia::input::report::TouchInputReport remove_touch_input_report;
    fuchsia::input::report::InputReport remove_input_report;
    remove_input_report.set_touch(std::move(remove_touch_input_report));
    input_reports.push_back(std::move(remove_input_report));
    fake_input_device_->SetReports(std::move(input_reports));

    return injection_times;
  }

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return display_width_; }
  uint32_t display_height() const { return display_height_; }

  fuchsia::sys::ComponentControllerPtr& client_component() { return client_component_; }

  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }
  Realm* realm() { return realm_.get(); }

  ResponseListenerServer* response_listener() { return response_listener_.get(); }

 private:
  void BuildRealm(const std::vector<std::pair<ChildName, LegacyUrl>>& components,
                  const std::vector<Route>& routes,
                  const std::vector<std::pair<ChildName, std::string>>& v2_components) {
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    response_listener_ = std::make_unique<ResponseListenerServer>(dispatcher());
    realm()->AddLocalChild(kMockResponseListener, response_listener_.get());

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : components) {
      realm()->AddLegacyChild(name, component);
    }

    for (const auto& [name, component] : v2_components) {
      realm()->AddChild(name, component);
    }

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : routes) {
      realm()->AddRoute(route);
    }

    ui_test_manager_->BuildRealm();

    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();
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

  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;

  std::unique_ptr<ResponseListenerServer> response_listener_;

  std::unique_ptr<scenic::Session> session_;

  fuchsia::input::injection::InputDeviceRegistryPtr registry_;
  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_input_device_;
  fuchsia::input::report::InputDevicePtr input_device_ptr_;

  int injection_count_ = 0;

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
        std::make_pair(kMemoryPressureProvider, kMemoryPressureProviderUrl),
        std::make_pair(kNetstack, kNetstackUrl),
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
    return {{.capabilities = {Protocol{test::touch::ResponseListener::Name_}},
             .source = ChildRef{kMockResponseListener},
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                              Protocol{fuchsia::sysmem::Allocator::Name_},
                              Protocol{fuchsia::tracing::provider::Registry::Name_},
                              Protocol{fuchsia::ui::scenic::Scenic::Name_},
                              Protocol{fuchsia::vulkan::loader::Loader::Name_}},
             .source = ParentRef(),
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
             .source = ChildRef{kMemoryPressureProvider},
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
             .source = ChildRef{kNetstack},
             .targets = {target}}};
  }

  static constexpr auto kFlutterRealm = "flutter_realm";
  static constexpr auto kFlutterRealmUrl = "#meta/one-flutter-realm.cm";

 private:
  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl = "#meta/netstack.cm";
};

class FlutterInputTestIp : public FlutterInputTestBase<> {};

INSTANTIATE_TEST_SUITE_P(FlutterInputTestIpParameterized, FlutterInputTestIp,
                         testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                         ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(FlutterInputTestIp, FlutterTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  bool injection_complete = false;
  SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                          input_injection_time,
                          /*component_name=*/"one-flutter", injection_complete);

  input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
  RunLoopUntil([&injection_complete] { return injection_complete; });
}

class FlutterSwipeTest : public FlutterInputTestBase<InjectSwipeParams> {};

INSTANTIATE_TEST_SUITE_P(
    FlutterSwipeTestParameterized, FlutterSwipeTest,
    testing::Combine(testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                     ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER),
                     testing::Values(GetRightSwipeParams(), GetDownwardSwipeParams(),
                                     GetLeftSwipeParams(), GetUpwardSwipeParams())));

TEST_P(FlutterSwipeTest, SwipeTest) {
  const auto& [direction, swipe_length, begin_x, begin_y, expected_events] =
      std::get<1>(GetParam());
  std::vector<test::touch::PointerData> actual_events;
  response_listener()->SetRespondCallback(
      [&actual_events](auto touch) { actual_events.push_back(std::move(touch)); });

  // Inject a swipe on the display. As the child view is rotated by 90 degrees, the direction of the
  // swipe also gets rotated by 90 degrees.
  auto injection_times =
      InjectSwipe<zx::basic_time<ZX_CLOCK_MONOTONIC>>(direction, swipe_length, begin_x, begin_y);

  //  Client sends a response for 1 Down and |swipe_length| Move PointerEventPhase events.
  RunLoopUntil([&actual_events, expected_events = swipe_length] {
    return actual_events.size() >= static_cast<uint32_t>(expected_events + 1);
  });

  ASSERT_EQ(actual_events.size(), expected_events.size());
  ASSERT_EQ(actual_events.size(), injection_times.size());
  AssertSwipeEvents(actual_events, expected_events, injection_times);
}

template <typename... Ts>
class GfxInputTestIpBase : public TouchInputBase<Ts...> {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
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
         .source = ParentRef(),
         .targets = {ChildRef{kCppGfxClient}}},
    };
  }

 private:
  static constexpr auto kCppGfxClient = "gfx_client";
  static constexpr auto kCppGfxClientUrl = "#meta/touch-gfx-client.cm";
};

class GfxInputTestIp : public GfxInputTestIpBase<> {};
INSTANTIATE_TEST_SUITE_P(GfxInputTestIpParametized, GfxInputTestIp,
                         testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                         ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(GfxInputTestIp, CppGfxClientTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  bool injection_complete = false;
  SetResponseExpectations(/*expected_x=*/static_cast<float>(display_height()) / 4.f,
                          /*expected_y=*/static_cast<float>(display_width()) / 4.f,
                          input_injection_time,
                          /*component_name=*/"touch-gfx-client", injection_complete);

  input_injection_time = InjectInput<zx::basic_time<ZX_CLOCK_MONOTONIC>>(TapLocation::kTopLeft);
  RunLoopUntil([&injection_complete] { return injection_complete; });
}

class GfxSwipeTest : public GfxInputTestIpBase<InjectSwipeParams> {};

INSTANTIATE_TEST_SUITE_P(
    GfxSwipeTestParameterized, GfxSwipeTest,
    testing::Combine(testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                     ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER),
                     testing::Values(GetRightSwipeParams(), GetDownwardSwipeParams(),
                                     GetLeftSwipeParams(), GetUpwardSwipeParams())));

TEST_P(GfxSwipeTest, CppGFXClientSwipeTest) {
  const auto& [direction, swipe_length, begin_x, begin_y, expected_events] =
      std::get<1>(GetParam());
  std::vector<test::touch::PointerData> actual_events;
  response_listener()->SetRespondCallback(
      [&actual_events](auto touch) { actual_events.push_back(std::move(touch)); });

  // Inject a swipe on the display. As the child view is rotated by 90 degrees, the direction of the
  // swipe also gets rotated by 90 degrees.
  auto injection_times =
      InjectSwipe<zx::basic_time<ZX_CLOCK_MONOTONIC>>(direction, swipe_length, begin_x, begin_y);

  //  Client sends a response for every PointerEventPhase event which includes 1 Add, 1 Down,
  // |swipe_length| Move, 1 Up, 1 Remove.
  RunLoopUntil([&actual_events, expected_events = swipe_length] {
    return actual_events.size() >= static_cast<uint32_t>(expected_events + 4);
  });

  // Remove the first event received as it is a response sent for an Add event.
  actual_events.erase(actual_events.begin());

  std::vector<ExpectedSwipeEvent> mutable_expected_events = expected_events;

  // The |ExpectedSwipeEvent| for Up and Remove PointerEventPhase will be the same as the last
  // Move event.
  auto last_touch_event = mutable_expected_events.back();
  mutable_expected_events.push_back(last_touch_event);
  mutable_expected_events.push_back(last_touch_event);

  // The time for Up and Remove PointerEventPhase will be the same as the last Move event.
  auto last_touch_time = injection_times.back();
  injection_times.push_back(last_touch_time);
  injection_times.push_back(last_touch_time);

  ASSERT_EQ(actual_events.size(), mutable_expected_events.size());
  ASSERT_EQ(actual_events.size(), injection_times.size());
  AssertSwipeEvents(actual_events, mutable_expected_events, injection_times);
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
  void TryInject(time_utc* input_injection_time) {
    *input_injection_time = InjectInput<time_utc>(TapLocation::kTopLeft);
    async::PostDelayedTask(
        dispatcher(), [this, input_injection_time] { TryInject(input_injection_time); },
        kTapRetryInterval);
  }

  // Helper method for checking the test.touch.ResponseListener response from a web app.
  void SetResponseExpectationsWeb(float expected_x, float expected_y,
                                  time_utc& input_injection_time, std::string const& component_name,
                                  bool& injection_complete) {
    response_listener()->SetRespondCallback(
        [expected_x, expected_y, component_name, &injection_complete,
         &input_injection_time](test::touch::PointerData pointer_data) {
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
          EXPECT_EQ(pointer_data.component_name(), component_name);

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
        {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
         .source = ChildRef{kMemoryPressureProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::net::interfaces::State::Name_},
                          Protocol{fuchsia::netstack::Netstack::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_},
                          Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
         .source = ChildRef{kWebContextProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kFontsProvider}}},
        {.capabilities = {Protocol{fuchsia::cobalt::LoggerFactory::Name_}},
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
                         testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                         ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(WebEngineTestIp, ChromiumTap) {
  // Use a UTC time for compatibility with the time reported by `Date.now()` in web-engine.
  time_utc input_injection_time(0);

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
        {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_},
                          Protocol{test::touch::TestAppLauncher::Name_}},
         .source = ChildRef{kEmbeddingFlutter},
         .targets = {ParentRef()}},
        {.capabilities = {Protocol{test::touch::ResponseListener::Name_}},
         .source = ChildRef{kMockResponseListener},
         .targets = {ChildRef{kEmbeddingFlutter}}},
        {.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
         .source = ParentRef(),
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

class FlutterInFlutterTestIp : public FlutterInputTestIp, public EmbeddingFlutterTestIp {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestV2Components() override {
    return merge({EmbeddingFlutterTestIp::GetEmbeddingFlutterComponents(),
                  FlutterInputTestIp::GetTestV2Components()});
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({EmbeddingFlutterTestIp::GetEmbeddingFlutterRoutes(),
                  FlutterInputTestIp::GetFlutterRoutes(ChildRef{kEmbeddingFlutter}),
                  FlutterInputTestIp::GetFlutterRoutes(ChildRef{kFlutterRealm}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kFlutterRealm},
                       .targets = {ChildRef{kEmbeddingFlutter}}},
                  }});
  }
};

INSTANTIATE_TEST_SUITE_P(FlutterInFlutterTestIpParameterized, FlutterInFlutterTestIp,
                         testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                         ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(FlutterInFlutterTestIp, FlutterInFlutterTap) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  // Launch the embedded app.
  LaunchEmbeddedClient("one-flutter");

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

  // Parent app takes up the right half of the screen. Expect response from it when injecting to
  // the right.
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
                         testing::Values(ui_testing::UITestRealm::SceneOwnerType::ROOT_PRESENTER,
                                         ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER));

TEST_P(WebInFlutterTestIp, WebInFlutterTap) {
  // Launch the embedded app.
  LaunchEmbeddedClient("one-chromium");

  // Parent app takes up the right half of the screen. Expect response from it when injecting to
  // the right.
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
