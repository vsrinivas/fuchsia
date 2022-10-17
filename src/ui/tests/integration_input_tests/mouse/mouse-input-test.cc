// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/fonts/cpp/fidl.h>
#include <fuchsia/input/report/cpp/fidl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <fuchsia/web/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/interface_ptr.h"
#include "src/ui/testing/util/portable_ui_test.h"

namespace {

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::ConfigValue;
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

// Combines all vectors in `vecs` into one.
template <typename T>
std::vector<T> merge(std::initializer_list<std::vector<T>> vecs) {
  std::vector<T> result;
  for (auto v : vecs) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

int ButtonsToInt(const std::vector<fuchsia::ui::test::input::MouseButton>& buttons) {
  int result = 0;
  for (const auto& button : buttons) {
    result |= (0x1 >> button);
  }

  return result;
}

// `MouseInputListener` is a local test protocol that our test apps use to let us know
// what position and button press state the mouse cursor has.
class MouseInputListenerServer : public fuchsia::ui::test::input::MouseInputListener,
                                 public LocalComponent {
 public:
  explicit MouseInputListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void ReportMouseInput(
      fuchsia::ui::test::input::MouseInputListenerReportMouseInputRequest request) override {
    events_.push(std::move(request));
  }

  // |MockComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    FX_CHECK(mock_handles->outgoing()->AddPublicService(
                 fidl::InterfaceRequestHandler<fuchsia::ui::test::input::MouseInputListener>(
                     [this](auto request) {
                       bindings_.AddBinding(this, std::move(request), dispatcher_);
                     })) == ZX_OK);
    mock_handles_.emplace_back(std::move(mock_handles));
  }

  size_t SizeOfEvents() const { return events_.size(); }

  fuchsia::ui::test::input::MouseInputListenerReportMouseInputRequest PopEvent() {
    auto e = std::move(events_.front());
    events_.pop();
    return e;
  }

  const fuchsia::ui::test::input::MouseInputListenerReportMouseInputRequest& LastEvent() const {
    return events_.back();
  }

  void ClearEvents() { events_ = {}; }

 private:
  // Not owned.
  async_dispatcher_t* dispatcher_ = nullptr;
  fidl::BindingSet<fuchsia::ui::test::input::MouseInputListener> bindings_;
  std::vector<std::unique_ptr<LocalComponentHandles>> mock_handles_;
  std::queue<fuchsia::ui::test::input::MouseInputListenerReportMouseInputRequest> events_;
};

constexpr auto kMouseInputListener = "mouse_input_listener";

struct Position {
  double x = 0.0;
  double y = 0.0;
};

class MouseInputBase : public ui_testing::PortableUITest {
 protected:
  MouseInputBase()
      : mouse_input_listener_(std::make_unique<MouseInputListenerServer>(dispatcher())) {}

  std::string GetTestUIStackUrl() override { return "#meta/test-ui-stack.cm"; }

  MouseInputListenerServer* mouse_input_listener() { return mouse_input_listener_.get(); }

  void SetUp() override {
    ui_testing::PortableUITest::SetUp();

    // Register fake mouse device.
    RegisterMouse();

    // Get the display dimensions.
    FX_LOGS(INFO) << "Waiting for scenic display info";
    auto scenic = realm_root()->Connect<fuchsia::ui::scenic::Scenic>();
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
    ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 0u);
  }

  // Subclass should implement this method to add components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() { return {}; }

  // Subclass should implement this method to add v2 components to the test realm
  // next to the base ones added.
  virtual std::vector<std::pair<ChildName, std::string>> GetTestV2Components() { return {}; }

  // Subclass should implement this method to add capability routes to the test
  // realm next to the base ones added.
  virtual std::vector<Route> GetTestRoutes() { return {}; }

  // Helper method for checking the test.mouse.MouseInputListener response from the client app.
  void VerifyEvent(
      fuchsia::ui::test::input::MouseInputListenerReportMouseInputRequest& pointer_data,
      double expected_x, double expected_y,
      std::vector<fuchsia::ui::test::input::MouseButton> expected_buttons,
      const fuchsia::ui::test::input::MouseEventPhase expected_phase,
      const std::string& component_name) {
    FX_LOGS(INFO) << "Client received mouse change at (" << pointer_data.local_x() << ", "
                  << pointer_data.local_y() << ") with buttons "
                  << ButtonsToInt(pointer_data.buttons()) << ".";
    FX_LOGS(INFO) << "Expected mouse change is at approximately (" << expected_x << ", "
                  << expected_y << ") with buttons " << ButtonsToInt(expected_buttons) << ".";

    // Allow for minor rounding differences in coordinates.
    // Note: These approximations don't account for `PointerMotionDisplayScaleHandler`
    // or `PointerMotionSensorScaleHandler`. We will need to do so in order to validate
    // larger motion or different sized displays.
    EXPECT_NEAR(pointer_data.local_x(), expected_x, 1);
    EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);
    EXPECT_EQ(pointer_data.buttons(), expected_buttons);
    EXPECT_EQ(pointer_data.phase(), expected_phase);
    EXPECT_EQ(pointer_data.component_name(), component_name);
  }

  void VerifyEventLocationOnTheRightOfExpectation(
      fuchsia::ui::test::input::MouseInputListenerReportMouseInputRequest& pointer_data,
      double expected_x_min, double expected_y,
      std::vector<fuchsia::ui::test::input::MouseButton> expected_buttons,
      const fuchsia::ui::test::input::MouseEventPhase expected_phase,
      const std::string& component_name) {
    FX_LOGS(INFO) << "Client received mouse change at (" << pointer_data.local_x() << ", "
                  << pointer_data.local_y() << ") with buttons "
                  << ButtonsToInt(pointer_data.buttons()) << ".";
    FX_LOGS(INFO) << "Expected mouse change is at approximately (>" << expected_x_min << ", "
                  << expected_y << ") with buttons " << ButtonsToInt(expected_buttons) << ".";

    EXPECT_GT(pointer_data.local_x(), expected_x_min);
    EXPECT_NEAR(pointer_data.local_y(), expected_y, 1);
    EXPECT_EQ(pointer_data.buttons(), expected_buttons);
    EXPECT_EQ(pointer_data.phase(), expected_phase);
    EXPECT_EQ(pointer_data.component_name(), component_name);
  }

  void ExtendRealm() override {
    // Key part of service setup: have this test component vend the
    // |MouseInputListener| service in the constructed realm.
    realm_builder()->AddLocalChild(kMouseInputListener, mouse_input_listener());

    // Expose scenic to the test fixture.
    realm_builder()->AddRoute({.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
                               .source = kTestUIStackRef,
                               .targets = {ParentRef()}});

    // Configure test-ui-stack.
    realm_builder()->InitMutableConfigToEmpty(kTestUIStack);
    realm_builder()->SetConfigValue(kTestUIStack, "use_scene_manager", ConfigValue::Bool(true));
    realm_builder()->SetConfigValue(kTestUIStack, "use_flatland", ConfigValue::Bool(true));
    realm_builder()->SetConfigValue(kTestUIStack, "display_rotation", ConfigValue::Uint32(0));

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

  // Guaranteed to be initialized after SetUp().
  uint32_t display_width() const { return display_width_; }
  uint32_t display_height() const { return display_height_; }

  std::unique_ptr<MouseInputListenerServer> mouse_input_listener_;

 private:
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
};

class FlutterInputTest : public MouseInputBase {
 protected:
  std::vector<std::pair<ChildName, std::string>> GetTestV2Components() override {
    return {
        std::make_pair(kMouseInputFlutter, kMouseInputFlutterUrl),
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
                     Protocol{fuchsia::ui::test::input::MouseInputListener::Name_},
                 },
             .source = ChildRef{kMouseInputListener},
             .targets = {target}},
            {.capabilities =
                 {
                     Protocol{fuchsia::ui::composition::Allocator::Name_},
                     Protocol{fuchsia::ui::composition::Flatland::Name_},
                     Protocol{fuchsia::ui::scenic::Scenic::Name_},
                 },
             .source = kTestUIStackRef,
             .targets = {target}},
            {.capabilities =
                 {
                     // Redirect logging output for the test realm to
                     // the host console output.
                     Protocol{fuchsia::logger::LogSink::Name_},
                     Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                     Protocol{fuchsia::sysmem::Allocator::Name_},
                     Protocol{fuchsia::tracing::provider::Registry::Name_},
                     Protocol{fuchsia::vulkan::loader::Loader::Name_},
                 },
             .source = ParentRef(),
             .targets = {target}}};
  }

  static constexpr auto kMouseInputFlutter = "mouse-input-flutter";
  static constexpr auto kMouseInputFlutterUrl = "#meta/mouse-input-flutter-realm.cm";
};

TEST_F(FlutterInputTest, FlutterMouseMove) {
  LaunchClient();

  SimulateMouseEvent(/* pressed_buttons = */ {}, /* movement_x = */ 1, /* movement_y = */ 2);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 1u);

  auto e = mouse_input_listener_->PopEvent();

  // If the first mouse event is cursor movement, Flutter first sends an ADD event with updated
  // location.
  VerifyEvent(e,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f + 1,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f + 2,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::ADD,
              /*component_name=*/"mouse-input-flutter");
}

TEST_F(FlutterInputTest, FlutterMouseDown) {
  LaunchClient();

  SimulateMouseEvent(/* pressed_buttons = */ {fuchsia::ui::test::input::MouseButton::FIRST},
                     /* movement_x = */ 0, /* movement_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 3; });

  ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 3u);

  auto event_add = mouse_input_listener_->PopEvent();
  auto event_down = mouse_input_listener_->PopEvent();
  auto event_noop_move = mouse_input_listener_->PopEvent();

  // If the first mouse event is a button press, Flutter first sends an ADD event with no buttons.
  VerifyEvent(event_add,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::ADD,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a DOWN pointer event with the buttons we care about.
  VerifyEvent(event_down,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::DOWN,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a MOVE pointer event with no new information.
  VerifyEvent(event_noop_move,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::MOVE,
              /*component_name=*/"mouse-input-flutter");
}

TEST_F(FlutterInputTest, FlutterMouseDownUp) {
  LaunchClient();

  SimulateMouseEvent(/* pressed_buttons = */ {fuchsia::ui::test::input::MouseButton::FIRST},
                     /* movement_x = */ 0, /* movement_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 3; });

  ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 3u);

  auto event_add = mouse_input_listener_->PopEvent();
  auto event_down = mouse_input_listener_->PopEvent();
  auto event_noop_move = mouse_input_listener_->PopEvent();

  // If the first mouse event is a button press, Flutter first sends an ADD event with no buttons.
  VerifyEvent(event_add,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::ADD,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a DOWN pointer event with the buttons we care about.
  VerifyEvent(event_down,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::DOWN,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a MOVE pointer event with no new information.
  VerifyEvent(event_noop_move,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::MOVE,
              /*component_name=*/"mouse-input-flutter");

  SimulateMouseEvent(/* pressed_buttons = */ {}, /* movement_x = */ 0, /* movement_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 1u);

  auto event_up = mouse_input_listener_->PopEvent();
  VerifyEvent(event_up,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::UP,
              /*component_name=*/"mouse-input-flutter");
}

TEST_F(FlutterInputTest, FlutterMouseDownMoveUp) {
  LaunchClient();

  SimulateMouseEvent(/* pressed_buttons = */ {fuchsia::ui::test::input::MouseButton::FIRST},
                     /* movement_x = */ 0, /* movement_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 3; });

  ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 3u);

  auto event_add = mouse_input_listener_->PopEvent();
  auto event_down = mouse_input_listener_->PopEvent();
  auto event_noop_move = mouse_input_listener_->PopEvent();

  // If the first mouse event is a button press, Flutter first sends an ADD event with no buttons.
  VerifyEvent(event_add,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::ADD,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a DOWN pointer event with the buttons we care about.
  VerifyEvent(event_down,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::DOWN,
              /*component_name=*/"mouse-input-flutter");

  // Then Flutter sends a MOVE pointer event with no new information.
  VerifyEvent(event_noop_move,
              /*expected_x=*/static_cast<double>(display_width()) / 2.f,
              /*expected_y=*/static_cast<double>(display_height()) / 2.f,
              /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::MOVE,
              /*component_name=*/"mouse-input-flutter");

  SimulateMouseEvent(/* pressed_buttons = */ {fuchsia::ui::test::input::MouseButton::FIRST},
                     /* movement_x = */ kClickToDragThreshold, /* movement_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 1u);

  auto event_move = mouse_input_listener_->PopEvent();

  VerifyEventLocationOnTheRightOfExpectation(
      event_move,
      /*expected_x_min=*/static_cast<double>(display_width()) / 2.f + 1,
      /*expected_y=*/static_cast<double>(display_height()) / 2.f,
      /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
      /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::MOVE,
      /*component_name=*/"mouse-input-flutter");

  SimulateMouseEvent(/* pressed_buttons = */ {}, /* movement_x = */ 0, /* movement_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  ASSERT_EQ(mouse_input_listener_->SizeOfEvents(), 1u);

  auto event_up = mouse_input_listener_->PopEvent();

  VerifyEventLocationOnTheRightOfExpectation(
      event_up,
      /*expected_x_min=*/static_cast<double>(display_width()) / 2.f + 1,
      /*expected_y=*/static_cast<double>(display_height()) / 2.f,
      /*expected_buttons=*/{},
      /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::UP,
      /*component_name=*/"mouse-input-flutter");
}

// TODO(fxbug.dev/103098): This test shows the issue when sending mouse wheel as the first event to
// Flutter.
// 1. expect Flutter app receive 2 events: ADD - Scroll, but got 3 events: Move - Scroll - Scroll.
// 2. the first event flutter app received has random value in buttons field
// Disabled until flutter rolls, since it changes the behavior of this issue.
TEST_F(FlutterInputTest, DISABLED_FlutterMouseWheelIssue103098) {
  LaunchClient();

  SimulateMouseScroll(/* pressed_buttons = */ {}, /* scroll_x = */ 1, /* scroll_y = */ 0);
  // Here we expected 2 events, ADD - Scroll, but got 3, Move - Scroll - Scroll.
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 3; });

  double initial_x = static_cast<double>(display_width()) / 2.f;
  double initial_y = static_cast<double>(display_height()) / 2.f;

  auto event_1 = mouse_input_listener_->PopEvent();
  EXPECT_NEAR(event_1.local_x(), initial_x, 1);
  EXPECT_NEAR(event_1.local_y(), initial_y, 1);
  // Flutter will scale the count of ticks to pixel.
  EXPECT_GT(event_1.wheel_x_physical_pixel(), 0);
  EXPECT_EQ(event_1.wheel_y_physical_pixel(), 0);
  EXPECT_EQ(event_1.phase(), fuchsia::ui::test::input::MouseEventPhase::MOVE);

  auto event_2 = mouse_input_listener_->PopEvent();
  VerifyEvent(event_2,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_phase=*/fuchsia::ui::test::input::MouseEventPhase::HOVER,
              /*component_name=*/"mouse-input-flutter");
  // Flutter will scale the count of ticks to pixel.
  EXPECT_GT(event_2.wheel_x_physical_pixel(), 0);
  EXPECT_EQ(event_2.wheel_y_physical_pixel(), 0);

  auto event_3 = mouse_input_listener_->PopEvent();
  VerifyEvent(event_3,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::HOVER,
              /*component_name=*/"mouse-input-flutter");
  // Flutter will scale the count of ticks to pixel.
  EXPECT_GT(event_3.wheel_x_physical_pixel(), 0);
  EXPECT_EQ(event_3.wheel_y_physical_pixel(), 0);
}

TEST_F(FlutterInputTest, FlutterMouseWheel) {
  LaunchClient();

  double initial_x = static_cast<double>(display_width()) / 2.f + 1;
  double initial_y = static_cast<double>(display_height()) / 2.f + 2;

  // TODO(fxbug.dev/103098): Send a mouse move as the first event to workaround.
  SimulateMouseEvent(/* pressed_buttons = */ {},
                     /* movement_x = */ 1, /* movement_y = */ 2);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  auto event_add = mouse_input_listener_->PopEvent();
  VerifyEvent(event_add,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::ADD,
              /*component_name=*/"mouse-input-flutter");

  SimulateMouseScroll(/* pressed_buttons = */ {}, /* scroll_x = */ 1, /* scroll_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  auto event_wheel_h = mouse_input_listener_->PopEvent();

  VerifyEvent(event_wheel_h,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_phase=*/fuchsia::ui::test::input::MouseEventPhase::HOVER,
              /*component_name=*/"mouse-input-flutter");
  // Flutter will scale the count of ticks to pixel.
  EXPECT_GT(event_wheel_h.wheel_x_physical_pixel(), 0);
  EXPECT_EQ(event_wheel_h.wheel_y_physical_pixel(), 0);

  SimulateMouseScroll(/* pressed_buttons = */ {}, /* scroll_x = */ 0, /* scroll_y = */ 1);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  auto event_wheel_v = mouse_input_listener_->PopEvent();

  VerifyEvent(event_wheel_v,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::HOVER,
              /*component_name=*/"mouse-input-flutter");
  // Flutter will scale the count of ticks to pixel.
  EXPECT_LT(event_wheel_v.wheel_y_physical_pixel(), 0);
  EXPECT_EQ(event_wheel_v.wheel_x_physical_pixel(), 0);
}

class ChromiumInputTest : public MouseInputBase {
 protected:
  std::vector<std::pair<ChildName, LegacyUrl>> GetTestComponents() override {
    return {
        std::make_pair(kWebContextProvider, kWebContextProviderUrl),
    };
  }

  std::vector<std::pair<ChildName, std::string>> GetTestV2Components() override {
    return {
        std::make_pair(kMouseInputChromium, kMouseInputChromiumUrl),
        std::make_pair(kBuildInfoProvider, kBuildInfoProviderUrl),
        std::make_pair(kMemoryPressureProvider, kMemoryPressureProviderUrl),
        std::make_pair(kNetstack, kNetstackUrl),
        std::make_pair(kMockCobalt, kMockCobaltUrl),
    };
  }

  std::vector<Route> GetTestRoutes() override {
    return merge({GetChromiumRoutes(ChildRef{kMouseInputChromium}),
                  {
                      {.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                       .source = ChildRef{kMouseInputChromium},
                       .targets = {ParentRef()}},
                  }});
  }

  // Routes needed to setup Chromium client.
  static std::vector<Route> GetChromiumRoutes(ChildRef target) {
    return {
        {.capabilities =
             {
                 Protocol{fuchsia::accessibility::semantics::SemanticsManager::Name_},
                 Protocol{fuchsia::ui::composition::Allocator::Name_},
                 Protocol{fuchsia::ui::composition::Flatland::Name_},
                 Protocol{fuchsia::ui::scenic::Scenic::Name_},
             },
         .source = kTestUIStackRef,
         .targets = {target}},
        {.capabilities =
             {
                 Protocol{fuchsia::sys::Environment::Name_},
                 Protocol{fuchsia::logger::LogSink::Name_},
                 Protocol{fuchsia::vulkan::loader::Loader::Name_},
             },
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::ui::test::input::MouseInputListener::Name_}},
         .source = ChildRef{kMouseInputListener},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
         .source = ChildRef{kMemoryPressureProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::netstack::Netstack::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::net::interfaces::State::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::web::ContextProvider::Name_}},
         .source = ChildRef{kWebContextProvider},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::sys::Environment::Name_},
                          Protocol{fuchsia::logger::LogSink::Name_}},
         .source = ParentRef(),
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::metrics::MetricEventLoggerFactory::Name_}},
         .source = ChildRef{kMockCobalt},
         .targets = {ChildRef{kMemoryPressureProvider}}},
        {.capabilities = {Protocol{fuchsia::sysmem::Allocator::Name_}},
         .source = ParentRef(),
         .targets = {ChildRef{kMemoryPressureProvider}, ChildRef{kMouseInputChromium}}},
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
        {.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
         .source = ChildRef{kNetstack},
         .targets = {target}},
        {.capabilities = {Protocol{fuchsia::buildinfo::Provider::Name_}},
         .source = ChildRef{kBuildInfoProvider},
         .targets = {target, ChildRef{kWebContextProvider}}},
    };
  }

  // TODO(fxbug.dev/58322): EnsureMouseIsReadyAndGetPosition will send a mouse click
  // (down and up) and wait for response to ensure the mouse is ready to use. We will retry a mouse
  // click if we can not get the mouseup response in small timeout. This function returns
  // the cursor position in WebEngine coordinate system.
  Position EnsureMouseIsReadyAndGetPosition() {
    for (int retry = 0; retry < kMaxRetry; retry++) {
      // Mouse down and up.
      SimulateMouseEvent(/* pressed_buttons = */ {fuchsia::ui::test::input::MouseButton::FIRST},
                         /* movement_x = */ 0, /* movement_y = */ 0);
      SimulateMouseEvent(/* pressed_buttons = */ {}, /* movement_x = */ 0, /* movement_y = */ 0);

      RunLoopWithTimeoutOrUntil(
          [this] {
            return this->mouse_input_listener_->SizeOfEvents() > 0 &&
                   this->mouse_input_listener_->LastEvent().phase() ==
                       fuchsia::ui::test::input::MouseEventPhase::UP;
          },
          kFirstEventRetryInterval);
      if (mouse_input_listener_->SizeOfEvents() > 0 &&
          mouse_input_listener_->LastEvent().phase() ==
              fuchsia::ui::test::input::MouseEventPhase::UP) {
        Position p;
        p.x = mouse_input_listener_->LastEvent().local_x();
        p.y = mouse_input_listener_->LastEvent().local_y();
        mouse_input_listener_->ClearEvents();
        return p;
      }
    }

    FX_LOGS(FATAL) << "Can not get mouse click in max retries " << kMaxRetry;
    return Position{};
  }

  static constexpr auto kMouseInputChromium = "mouse-input-chromium";
  static constexpr auto kMouseInputChromiumUrl = "#meta/mouse-input-chromium.cm";

  static constexpr auto kWebContextProvider = "web_context_provider";
  static constexpr auto kWebContextProviderUrl =
      "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx";

  static constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
  static constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

  static constexpr auto kNetstack = "netstack";
  static constexpr auto kNetstackUrl = "#meta/netstack.cm";

  static constexpr auto kBuildInfoProvider = "build_info_provider";
  static constexpr auto kBuildInfoProviderUrl = "#meta/fake_build_info.cm";

  static constexpr auto kMockCobalt = "cobalt";
  static constexpr auto kMockCobaltUrl = "#meta/mock_cobalt.cm";

  // The first event to WebEngine may lost, see EnsureMouseIsReadyAndGetPosition. Retry to ensure
  // WebEngine is ready to process events.
  static constexpr auto kFirstEventRetryInterval = zx::sec(1);

  // To avoid retry to timeout, limit 10 retries, if still not ready, fail it with meaningful error.
  static const int kMaxRetry = 10;
};

TEST_F(ChromiumInputTest, ChromiumMouseMove) {
  LaunchClient();

  auto initial_position = EnsureMouseIsReadyAndGetPosition();

  double initial_x = initial_position.x;
  double initial_y = initial_position.y;

  SimulateMouseEvent(/* pressed_buttons = */ {},
                     /* movement_x = */ 5, /* movement_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  auto event_move = mouse_input_listener_->PopEvent();

  VerifyEventLocationOnTheRightOfExpectation(
      event_move,
      /*expected_x_min=*/initial_x,
      /*expected_y=*/initial_y,
      /*expected_buttons=*/{},
      /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::MOVE,
      /*component_name=*/"mouse-input-chromium");
}

TEST_F(ChromiumInputTest, ChromiumMouseDownMoveUp) {
  LaunchClient();

  auto initial_position = EnsureMouseIsReadyAndGetPosition();

  double initial_x = initial_position.x;
  double initial_y = initial_position.y;

  SimulateMouseEvent(/* pressed_buttons = */ {fuchsia::ui::test::input::MouseButton::FIRST},
                     /* movement_x = */ 0, /* movement_y = */ 0);
  SimulateMouseEvent(/* pressed_buttons = */ {fuchsia::ui::test::input::MouseButton::FIRST},
                     /* movement_x = */ kClickToDragThreshold, /* movement_y = */ 0);
  SimulateMouseEvent(/* pressed_buttons = */ {}, /* movement_x = */ 0, /* movement_y = */ 0);

  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 3; });

  auto event_down = mouse_input_listener_->PopEvent();
  auto event_move = mouse_input_listener_->PopEvent();
  auto event_up = mouse_input_listener_->PopEvent();

  VerifyEvent(event_down,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::DOWN,
              /*component_name=*/"mouse-input-chromium");
  VerifyEventLocationOnTheRightOfExpectation(
      event_move,
      /*expected_x_min=*/initial_x,
      /*expected_y=*/initial_y,
      /*expected_buttons=*/{fuchsia::ui::test::input::MouseButton::FIRST},
      /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::MOVE,
      /*component_name=*/"mouse-input-chromium");
  VerifyEvent(event_up,
              /*expected_x=*/event_move.local_x(),
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::UP,
              /*component_name=*/"mouse-input-chromium");
}

TEST_F(ChromiumInputTest, ChromiumMouseWheel) {
  LaunchClient();

  auto initial_position = EnsureMouseIsReadyAndGetPosition();

  double initial_x = initial_position.x;
  double initial_y = initial_position.y;

  SimulateMouseScroll(/* pressed_buttons = */ {}, /* scroll_x = */ 1, /* scroll_y = */ 0);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  auto event_wheel_h = mouse_input_listener_->PopEvent();

  VerifyEvent(event_wheel_h,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::WHEEL,
              /*component_name=*/"mouse-input-chromium");
  // Chromium will scale the count of ticks to pixel.
  // Positive delta in Fuchsia  means scroll left, and scroll left in JS is negative delta.
  EXPECT_LT(event_wheel_h.wheel_x_physical_pixel(), 0);
  EXPECT_EQ(event_wheel_h.wheel_y_physical_pixel(), 0);

  SimulateMouseScroll(/* pressed_buttons = */ {}, /* scroll_x = */ 0, /* scroll_y = */ 1);
  RunLoopUntil([this] { return this->mouse_input_listener_->SizeOfEvents() == 1; });

  auto event_wheel_v = mouse_input_listener_->PopEvent();

  VerifyEvent(event_wheel_v,
              /*expected_x=*/initial_x,
              /*expected_y=*/initial_y,
              /*expected_buttons=*/{},
              /*expected_type=*/fuchsia::ui::test::input::MouseEventPhase::WHEEL,
              /*component_name=*/"mouse-input-chromium");
  // Chromium will scale the count of ticks to pixel.
  // Positive delta in Fuchsia means scroll up, and scroll up in JS is negative delta.
  EXPECT_LT(event_wheel_v.wheel_y_physical_pixel(), 0);
  EXPECT_EQ(event_wheel_v.wheel_x_physical_pixel(), 0);
}

}  // namespace
