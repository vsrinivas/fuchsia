// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/input/report/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <test/inputsynthesis/cpp/fidl.h>
#include <test/mouse/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

namespace {

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

// Alias for Component child name as provided to Realm Builder.
using ChildName = std::string;

// Alias for Component Legacy URL as provided to Realm Builder.
using LegacyUrl = std::string;

// Maximum pointer movement during a clickpad press for the gesture to
// be guaranteed to be interpreted as a click. For movement greater than
// this value, upper layers may, e.g., interpret the gesture as a drag.
//
// This value corresponds to the one used to instantiate the ClickDragHandler
// registered by Input Pipeline in Scene Manager.
constexpr int64_t kClickToDragThreshold = 16.0;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// Combines all vectors in `vecs` into one.
template <typename T>
std::vector<T> merge(std::initializer_list<std::vector<T>> vecs) {
  std::vector<T> result;
  for (auto v : vecs) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

// `ResponseListener` is a local test protocol that our test Flutter app uses to let us know
// what position and button press state the mouse cursor has.
// No need to use mutex in ResponseListenerServer because `MouseInputBase` inherits from
// `gtest::RealLoopFixture`, `RealLoopFixture` inherits from `RealLoop`, `RealLoop` has-an
// `async::Loop`, `Loop::Run()` Runs the message loop on the same thread with test.
class ResponseListenerServer : public test::mouse::ResponseListener, public LocalComponent {
 public:
  explicit ResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |test::mouse::ResponseListener|
  void Respond(test::mouse::PointerData pointer_data) override {
    events_.push(std::move(pointer_data));
  }

  // |MockComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    // When this component starts, add a binding to the test.mouse.ResponseListener
    // protocol to this component's outgoing directory.
    FX_CHECK(mock_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<test::mouse::ResponseListener>([this](auto request) {
                   bindings_.AddBinding(this, std::move(request), dispatcher_);
                 })) == ZX_OK);
    mock_handles_.emplace_back(std::move(mock_handles));
  }

  size_t SizeOfEvents() { return events_.size(); }

  test::mouse::PointerData PopEvent() {
    test::mouse::PointerData e = std::move(events_.front());
    events_.pop();
    return e;
  }

 private:
  // Not owned.
  async_dispatcher_t* dispatcher_ = nullptr;
  fidl::BindingSet<test::mouse::ResponseListener> bindings_;
  std::vector<std::unique_ptr<LocalComponentHandles>> mock_handles_;
  std::queue<test::mouse::PointerData> events_;
};

constexpr auto kResponseListener = "response_listener";

class MouseInputBase : public gtest::RealLoopFixture {
 protected:
  MouseInputBase() : response_listener_(std::make_unique<ResponseListenerServer>(dispatcher())) {}

  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

  ResponseListenerServer* response_listener() { return response_listener_.get(); }

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    ui_testing::UITestManager::Config config;
    config.use_flatland = true;
    config.scene_owner = ui_testing::UITestManager::SceneOwnerType::SCENE_MANAGER;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestManager::AccessibilityOwnerType::FAKE;
    config.ui_to_client_services = {
        fuchsia::ui::scenic::Scenic::Name_, fuchsia::ui::composition::Flatland::Name_,
        fuchsia::ui::composition::Allocator::Name_, fuchsia::ui::input::ImeService::Name_,
        fuchsia::ui::input3::Keyboard::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    AssembleRealm(this->GetTestComponents(), this->GetTestRoutes());

    // Get the display dimensions.
    FX_LOGS(INFO) << "Waiting for scenic display info";
    auto scenic = realm_exposed_services()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
      display_width_ = display_info.width_in_px;
      display_height_ = display_info.height_in_px;
      FX_LOGS(INFO) << "Got display_width = " << display_width_
                    << " and display_height = " << display_height_;
    });
    RunLoopUntil([this] { return display_width_ != 0 && display_height_ != 0; });
  }

  void TearDown() override {
    // at the end of test, ensure event queue is empty.
    ASSERT_EQ(response_listener_->SizeOfEvents(), 0u);
  }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() { return {}; }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<Route> GetTestRoutes() { return {}; }

  // Helper method for checking the test.mouse.ResponseListener response from the client app.
  void VerifyEvent(test::mouse::PointerData& pointer_data, float expected_x, float expected_y,
                   int64_t expected_buttons, const std::string& expected_type,
                   zx::basic_time<ZX_CLOCK_MONOTONIC>& input_injection_time,
                   const std::string& component_name) {
    FX_LOGS(INFO) << "Client received mouse change at (" << pointer_data.local_x() << ", "
                  << pointer_data.local_y() << ") with buttons " << pointer_data.buttons() << ".";
    FX_LOGS(INFO) << "Expected mouse change is at approximately (" << expected_x << ", "
                  << expected_y << ") with buttons " << expected_buttons << ".";

    zx::duration elapsed_time =
        zx::basic_time<ZX_CLOCK_MONOTONIC>(pointer_data.time_received()) - input_injection_time;
    EXPECT_TRUE(elapsed_time.get() > 0 && elapsed_time.get() != ZX_TIME_INFINITE);
    FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
    FX_LOGS(INFO) << "Client Received Time (ns): " << pointer_data.time_received();
    FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();

    // Allow for minor rounding differences in coordinates.
    // Note: These approximations don't account for `PointerMotionDisplayScaleHandler`
    // or `PointerMotionSensorScaleHandler`. We will need to do so in order to validate
    // larger motion or different sized displays.
    EXPECT_NEAR(pointer_data.local_x(), expected_x, 1);
    EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);
    EXPECT_EQ(pointer_data.buttons(), expected_buttons);
    EXPECT_EQ(pointer_data.type(), expected_type);
    EXPECT_EQ(pointer_data.component_name(), component_name);
  }

  void VerifyEventLocationOnTheRightOfExpectation(
      test::mouse::PointerData& pointer_data, float expected_x_min, float expected_y,
      int64_t expected_buttons, const std::string& expected_type,
      zx::basic_time<ZX_CLOCK_MONOTONIC>& input_injection_time, const std::string& component_name) {
    FX_LOGS(INFO) << "Client received mouse change at (" << pointer_data.local_x() << ", "
                  << pointer_data.local_y() << ") with buttons " << pointer_data.buttons() << ".";
    FX_LOGS(INFO) << "Expected mouse change is at approximately (>" << expected_x_min << ", "
                  << expected_y << ") with buttons " << expected_buttons << ".";

    zx::duration elapsed_time =
        zx::basic_time<ZX_CLOCK_MONOTONIC>(pointer_data.time_received()) - input_injection_time;
    EXPECT_TRUE(elapsed_time.get() > 0 && elapsed_time.get() != ZX_TIME_INFINITE);
    FX_LOGS(INFO) << "Input Injection Time (ns): " << input_injection_time.get();
    FX_LOGS(INFO) << "Client Received Time (ns): " << pointer_data.time_received();
    FX_LOGS(INFO) << "Elapsed Time (ns): " << elapsed_time.to_nsecs();

    EXPECT_GT(pointer_data.local_x(), expected_x_min);
    EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);
    EXPECT_EQ(pointer_data.buttons(), expected_buttons);
    EXPECT_EQ(pointer_data.type(), expected_type);
    EXPECT_EQ(pointer_data.component_name(), component_name);
  }

  void AssembleRealm(const std::vector<std::pair<ChildName, LegacyUrl>>& components,
                     const std::vector<Route>& routes) {
    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    // Key part of service setup: have this test component vend the
    // |ResponseListener| service in the constructed realm.
    realm_->AddLocalChild(kResponseListener, response_listener());

    // Add components specific for this test case to the realm.
    for (const auto& [name, component] : components) {
      realm_->AddChild(name, component);
    }

    // Add the necessary routing for each of the extra components added above.
    for (const auto& route : routes) {
      realm_->AddRoute(route);
    }

    // Finally, build the realm using the provided components and routes.
    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->TakeExposedServicesDirectory();
  }

  void LaunchClient() {
    // Initialize scene, and attach client view.
    ui_test_manager_->InitializeScene();
    FX_LOGS(INFO) << "Wait for client view to render";
    RunLoopUntil([this]() { return ui_test_manager_->ClientViewIsRendering(); });
  }

  uint32_t AddMouseDevice(fidl::InterfacePtr<test::inputsynthesis::Mouse>& input_synthesis) {
    uint32_t device_id;
    bool new_device_completed = false;

    input_synthesis->AddDevice([&device_id, &new_device_completed](uint32_t id) {
      device_id = id;
      new_device_completed = true;
    });

    // wait for new device creation.
    RunLoopUntil([&new_device_completed] { return new_device_completed; });

    return device_id;
  }

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return display_width_; }
  uint32_t display_height() const { return display_height_; }

  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;

  std::unique_ptr<ResponseListenerServer> response_listener_;

 private:
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
};

class FlutterInputTest : public MouseInputBase {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {
        std::make_pair(kMouseInputFlutter, kMouseInputFlutterUrl),
        std::make_pair(kMemoryPressureProvider, kMemoryPressureProviderUrl),
        std::make_pair(kNetstack, kNetstackUrl),
    };
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({GetFlutterRoutes(ChildRef{kMouseInputFlutter}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kMouseInputFlutter},
                       .targets = {ParentRef()}},
                  }});
  }

  // Routes needed to setup Flutter client.
  static std::vector<Route> GetFlutterRoutes(ChildRef target) {
    return {{.capabilities =
                 {
                     Protocol{test::mouse::ResponseListener::Name_},
                 },
             .source = ChildRef{kResponseListener},
             .targets = {target}},
            {.capabilities =
                 {
                     Protocol{fuchsia::ui::composition::Allocator::Name_},
                     Protocol{fuchsia::ui::composition::Flatland::Name_},
                     Protocol{fuchsia::ui::scenic::Scenic::Name_},
                     // Redirect logging output for the test realm to
                     // the host console output.
                     Protocol{fuchsia::logger::LogSink::Name_},
                     Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                     Protocol{fuchsia::sysmem::Allocator::Name_},
                     Protocol{fuchsia::tracing::provider::Registry::Name_},
                     Protocol{fuchsia::vulkan::loader::Loader::Name_},
                 },
             .source = ParentRef(),
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
             .source = ChildRef{kMemoryPressureProvider},
             .targets = {target}},
            {.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
             .source = ChildRef{kNetstack},
             .targets = {target}}};
  }

  static constexpr auto kMouseInputFlutter = "mouse-input-flutter";
  static constexpr auto kMouseInputFlutterUrl = "#meta/mouse-input-flutter-realm.cm";

 private:
  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl = "#meta/netstack.cm";
};

TEST_F(FlutterInputTest, FlutterMouseMove) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient();
  auto input_synthesis = realm_exposed_services()->Connect<test::inputsynthesis::Mouse>();
  uint32_t device_id = AddMouseDevice(input_synthesis);

  bool injection_initiated = false;
  fuchsia::input::report::MouseInputReport report;
  report.set_movement_x(1);
  report.set_movement_y(2);
  auto ts = static_cast<uint64_t>(zx::clock::get_monotonic().get());
  input_synthesis->SendInputReport(
      device_id, std::move(report), ts, [&injection_initiated](auto result) {
        ASSERT_FALSE(result.is_err()) << "SendInputReport failed " << result.err();
        injection_initiated = true;
      });

  RunLoopUntil([&injection_initiated] { return injection_initiated; });

  RunLoopUntil([this] { return this->response_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(response_listener_->SizeOfEvents(), 1u);

  auto e = response_listener_->PopEvent();

  // If the first mouse event is cursor movement, Flutter first sends an ADD event with updated
  // location.
  VerifyEvent(e,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f + 1,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f + 2,
              /*expected_buttons=*/0,
              /*expected_type=*/"add", input_injection_time,
              /*component_name=*/"mouse-input-flutter");
}

TEST_F(FlutterInputTest, FlutterMouseDown) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient();
  auto input_synthesis = realm_exposed_services()->Connect<test::inputsynthesis::Mouse>();
  uint32_t device_id = AddMouseDevice(input_synthesis);

  bool injection_initiated = false;
  fuchsia::input::report::MouseInputReport report;
  report.set_movement_x(0);
  report.set_movement_y(0);
  report.set_pressed_buttons({0});
  auto ts = static_cast<uint64_t>(zx::clock::get_monotonic().get());
  input_synthesis->SendInputReport(
      device_id, std::move(report), ts, [&injection_initiated](auto result) {
        ASSERT_FALSE(result.is_err()) << "SendInputReport failed " << result.err();
        injection_initiated = true;
      });

  RunLoopUntil([&injection_initiated] { return injection_initiated; });
  RunLoopUntil([this] { return this->response_listener_->SizeOfEvents() == 3; });

  ASSERT_EQ(response_listener_->SizeOfEvents(), 3u);

  auto event_add = response_listener_->PopEvent();
  auto event_down = response_listener_->PopEvent();
  auto event_noop_move = response_listener_->PopEvent();

  // If the first mouse event is a button press, Flutter first sends an ADD event with no buttons.
  VerifyEvent(event_add,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/0,
              /*expected_type=*/"add", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a DOWN pointer event with the buttons we care about.
  VerifyEvent(event_down,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/fuchsia::ui::input::kMousePrimaryButton,
              /*expected_type=*/"down", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a MOVE pointer event with no new information.
  VerifyEvent(event_noop_move,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/fuchsia::ui::input::kMousePrimaryButton,
              /*expected_type=*/"move", input_injection_time,
              /*component_name=*/"mouse-input-flutter");
}

TEST_F(FlutterInputTest, FlutterMouseDownUp) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient();
  auto input_synthesis = realm_exposed_services()->Connect<test::inputsynthesis::Mouse>();
  uint32_t device_id = AddMouseDevice(input_synthesis);

  bool down_injection_initiated = false;
  fuchsia::input::report::MouseInputReport down_report;
  down_report.set_movement_x(0);
  down_report.set_movement_y(0);
  down_report.set_pressed_buttons({0});
  auto ts = static_cast<uint64_t>(zx::clock::get_monotonic().get());
  input_synthesis->SendInputReport(
      device_id, std::move(down_report), ts, [&down_injection_initiated](auto result) {
        ASSERT_FALSE(result.is_err()) << "SendInputReport failed " << result.err();
        down_injection_initiated = true;
      });

  RunLoopUntil([&down_injection_initiated] { return down_injection_initiated; });
  RunLoopUntil([this] { return this->response_listener_->SizeOfEvents() == 3; });

  ASSERT_EQ(response_listener_->SizeOfEvents(), 3u);

  auto event_add = response_listener_->PopEvent();
  auto event_down = response_listener_->PopEvent();
  auto event_noop_move = response_listener_->PopEvent();

  // If the first mouse event is a button press, Flutter first sends an ADD event with no buttons.
  VerifyEvent(event_add,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/0,
              /*expected_type=*/"add", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a DOWN pointer event with the buttons we care about.
  VerifyEvent(event_down,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/fuchsia::ui::input::kMousePrimaryButton,
              /*expected_type=*/"down", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a MOVE pointer event with no new information.
  VerifyEvent(event_noop_move,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/fuchsia::ui::input::kMousePrimaryButton,
              /*expected_type=*/"move", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  bool up_injection_initiated = false;
  fuchsia::input::report::MouseInputReport up_report;
  up_report.set_movement_x(0);
  up_report.set_movement_y(0);
  up_report.set_pressed_buttons({});
  input_synthesis->SendInputReport(
      device_id, std::move(up_report), ts, [&up_injection_initiated](auto result) {
        ASSERT_FALSE(result.is_err()) << "SendInputReport failed " << result.err();
        up_injection_initiated = true;
      });
  RunLoopUntil([&up_injection_initiated] { return up_injection_initiated; });
  RunLoopUntil([this] { return this->response_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(response_listener_->SizeOfEvents(), 1u);

  auto event_up = response_listener_->PopEvent();
  VerifyEvent(event_up,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/0,
              /*expected_type=*/"up", input_injection_time,
              /*component_name=*/"mouse-input-flutter");
}

TEST_F(FlutterInputTest, FlutterMouseDownMoveUp) {
  // Use `ZX_CLOCK_MONOTONIC` to avoid complications due to wall-clock time changes.
  zx::basic_time<ZX_CLOCK_MONOTONIC> input_injection_time(0);

  LaunchClient();
  auto input_synthesis = realm_exposed_services()->Connect<test::inputsynthesis::Mouse>();
  uint32_t device_id = AddMouseDevice(input_synthesis);

  bool down_injection_initiated = false;
  fuchsia::input::report::MouseInputReport down_report;
  down_report.set_movement_x(0);
  down_report.set_movement_y(0);
  down_report.set_pressed_buttons({0});
  auto ts = static_cast<uint64_t>(zx::clock::get_monotonic().get());
  input_synthesis->SendInputReport(
      device_id, std::move(down_report), ts, [&down_injection_initiated](auto result) {
        ASSERT_FALSE(result.is_err()) << "SendInputReport failed " << result.err();
        down_injection_initiated = true;
      });

  RunLoopUntil([&down_injection_initiated] { return down_injection_initiated; });
  RunLoopUntil([&down_injection_initiated] { return down_injection_initiated; });
  RunLoopUntil([this] { return this->response_listener_->SizeOfEvents() == 3; });

  ASSERT_EQ(response_listener_->SizeOfEvents(), 3u);

  auto event_add = response_listener_->PopEvent();
  auto event_down = response_listener_->PopEvent();
  auto event_noop_move = response_listener_->PopEvent();

  // If the first mouse event is a button press, Flutter first sends an ADD event with no buttons.
  VerifyEvent(event_add,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/0,
              /*expected_type=*/"add", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a DOWN pointer event with the buttons we care about.
  VerifyEvent(event_down,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/fuchsia::ui::input::kMousePrimaryButton,
              /*expected_type=*/"down", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a MOVE pointer event with no new information.
  VerifyEvent(event_noop_move,
              /*expected_x=*/static_cast<float>(display_width()) / 2.f,
              /*expected_y=*/static_cast<float>(display_height()) / 2.f,
              /*expected_buttons=*/fuchsia::ui::input::kMousePrimaryButton,
              /*expected_type=*/"move", input_injection_time,
              /*component_name=*/"mouse-input-flutter");

  bool move_injection_initiated = false;
  fuchsia::input::report::MouseInputReport move_report;
  // We use `kClickToDragThreshold` to make sure the mouse handler registers movement.
  move_report.set_movement_x(kClickToDragThreshold);
  move_report.set_pressed_buttons({0});
  input_synthesis->SendInputReport(
      device_id, std::move(move_report), ts, [&move_injection_initiated](auto result) {
        ASSERT_FALSE(result.is_err()) << "SendInputReport failed " << result.err();
        move_injection_initiated = true;
      });
  RunLoopUntil([&move_injection_initiated] { return move_injection_initiated; });
  RunLoopUntil([this] { return this->response_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(response_listener_->SizeOfEvents(), 1u);

  auto event_move = response_listener_->PopEvent();

  VerifyEventLocationOnTheRightOfExpectation(
      event_move,
      /*expected_x_min=*/static_cast<float>(display_width()) / 2.f + 1,
      /*expected_y=*/static_cast<float>(display_height()) / 2.f,
      /*expected_buttons=*/fuchsia::ui::input::kMousePrimaryButton,
      /*expected_type=*/"move", input_injection_time,
      /*component_name=*/"mouse-input-flutter");

  bool up_injection_initiated = false;
  fuchsia::input::report::MouseInputReport up_report;
  up_report.set_movement_x(0);
  up_report.set_movement_y(0);
  up_report.set_pressed_buttons({});
  input_synthesis->SendInputReport(
      device_id, std::move(up_report), ts, [&up_injection_initiated](auto result) {
        ASSERT_FALSE(result.is_err()) << "SendInputReport failed " << result.err();
        up_injection_initiated = true;
      });
  RunLoopUntil([&up_injection_initiated] { return up_injection_initiated; });
  RunLoopUntil([this] { return this->response_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(response_listener_->SizeOfEvents(), 1u);

  auto event_up = response_listener_->PopEvent();

  VerifyEventLocationOnTheRightOfExpectation(
      event_up,
      /*expected_x_min=*/static_cast<float>(display_width()) / 2.f + 1,
      /*expected_y=*/static_cast<float>(display_height()) / 2.f,
      /*expected_buttons=*/0,
      /*expected_type=*/"up", input_injection_time,
      /*component_name=*/"mouse-input-flutter");
}

}  // namespace
