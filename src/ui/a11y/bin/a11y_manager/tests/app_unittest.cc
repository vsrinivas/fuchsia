// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/app.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include "gtest/gtest.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_color_transform_handler.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_pointer_event_registry.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_settings_provider.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_setui_accessibility.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::SettingsManagerStatus;
using fuchsia::accessibility::SettingsPtr;
using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
using fuchsia::accessibility::semantics::Role;
using fuchsia::ui::input::accessibility::EventHandling;
using fuchsia::ui::input::accessibility::PointerEventListener;
using fuchsia::ui::input::accessibility::PointerEventListenerPtr;

const std::string kSemanticTreeSingle = "Node_id: 0, Label:Label A";
constexpr int kMaxLogBufferSize = 1024;

// clang-format off
constexpr std::array<float, 9> kIdentityMatrix = {
    1, 0, 0,
    0, 1, 0,
    0, 0, 1};

// clang-format on

class AppUnitTest : public gtest::TestLoopFixture {
 public:
  AppUnitTest() { context_ = context_provider_.context(); }
  void SetUp() override {
    TestLoopFixture::SetUp();

    zx::eventpair::create(0u, &eventpair_, &eventpair_peer_);
    view_ref_ = fuchsia::ui::views::ViewRef({
        .reference = std::move(eventpair_),
    });
  }

  void SendPointerEvents(PointerEventListener* listener, const std::vector<PointerParams>& events) {
    for (const auto& params : events) {
      SendPointerEvent(listener, params);
    }
  }

  void SendPointerEvent(PointerEventListener* listener, const PointerParams& params) {
    listener->OnEvent(ToPointerEvent(params, input_event_time_++));
  }

  // Sends a gesture that wouldn't be recognized by any accessibility feature, for testing arena
  // configuration.
  //
  // Returns the |handled| argument of the (last) resulting |OnStreamHandled| invocation.
  //
  // Yo dawg, I heard you like pointer event listener pointers, so I took a pointer to your pointer
  // event listener pointer so you can receive events while you receive events (while honoring the
  // C++ style guide).
  std::optional<EventHandling> SendUnrecognizedGesture(PointerEventListenerPtr* listener) {
    std::optional<EventHandling> event_handling;
    listener->events().OnStreamHandled =
        [&event_handling](uint32_t, uint32_t, EventHandling handled) { event_handling = handled; };

    SendPointerEvents(listener->get(), Zip({TapEvents(1, {}), TapEvents(2, {})}));

    RunLoopUntilIdle();
    return event_handling;
  }

  zx::eventpair eventpair_, eventpair_peer_;
  sys::ComponentContext* context_;
  sys::testing::ComponentContextProvider context_provider_;
  fuchsia::ui::views::ViewRef view_ref_;

 private:
  // We don't actually use these times. If we did, we'd want to more closely correlate them with
  // fake time.
  uint64_t input_event_time_ = 0;
};

// Create a test node with only a node id and a label.
Node CreateTestNode(uint32_t node_id, std::string label) {
  Node node = Node();
  node.set_node_id(node_id);
  node.set_child_ids({});
  node.set_role(Role::UNKNOWN);
  node.set_attributes(Attributes());
  node.mutable_attributes()->set_label(std::move(label));
  fuchsia::ui::gfx::BoundingBox box;
  node.set_location(std::move(box));
  fuchsia::ui::gfx::mat4 transform;
  node.set_transform(std::move(transform));
  return node;
}

// Test to make sure SemanticsManager Service is exposed by A11y.
// Test sends a node update to SemanticsManager and then compare the expected
// result using log file created by semantics manager.
TEST_F(AppUnitTest, UpdateNodeToSemanticsManager) {
  a11y_manager::App app = a11y_manager::App(context_provider_.TakeContext());
  RunLoopUntilIdle();

  // Create ViewRef.
  fuchsia::ui::views::ViewRef view_ref_connection;
  fidl::Clone(view_ref_, &view_ref_connection);

  // Create ActionListener.
  accessibility_test::MockSemanticListener semantic_listener(&context_provider_,
                                                             std::move(view_ref_connection));
  // We make sure the Semantic Listener has finished connecting to the
  // root.
  RunLoopUntilIdle();

  // Creating test node to update.
  std::vector<Node> update_nodes;
  Node node = CreateTestNode(0, "Label A");
  Node clone_node;
  node.Clone(&clone_node);
  update_nodes.push_back(std::move(clone_node));

  // Update the node created above.
  semantic_listener.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_listener.CommitUpdates();
  RunLoopUntilIdle();

  // Check that the committed node is present in the semantic tree.
  vfs::PseudoDir* debug_dir = context_->outgoing()->debug_dir();
  vfs::internal::Node* test_node;
  ASSERT_EQ(ZX_OK, debug_dir->Lookup(std::to_string(a11y::GetKoid(view_ref_)), &test_node));

  char buffer[kMaxLogBufferSize];
  ReadFile(test_node, kSemanticTreeSingle.size(), buffer);
  EXPECT_EQ(kSemanticTreeSingle, buffer);
}

// Test to make sure SettingsManager Service is exposed by A11y.
// Test sends connects a fake settings provider to SettingsManager, and make
// sure App gets the updates.
TEST_F(AppUnitTest, VerifyAppSettingsWatcher) {
  a11y_manager::App app = a11y_manager::App(context_provider_.TakeContext());
  RunLoopUntilIdle();

  // Create Settings Service.
  MockSettingsProvider settings_provider(&context_provider_);
  RunLoopUntilIdle();

  // Verify default values of settings in App.
  float kDefaultZoomFactor = 1.0;
  SettingsPtr settings = app.GetSettings();
  EXPECT_TRUE(settings->has_magnification_enabled());
  EXPECT_FALSE(settings->magnification_enabled());
  EXPECT_TRUE(settings->has_magnification_zoom_factor());
  EXPECT_EQ(kDefaultZoomFactor, settings->magnification_zoom_factor());
  EXPECT_TRUE(settings->has_screen_reader_enabled());
  EXPECT_FALSE(settings->screen_reader_enabled());
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_FALSE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::DISABLED, settings->color_correction());
  EXPECT_TRUE(settings->has_color_adjustment_matrix());
  EXPECT_EQ(kIdentityMatrix, settings->color_adjustment_matrix());

  // Change settings and verify the changes are reflected in App.
  SettingsManagerStatus status = SettingsManagerStatus::OK;
  settings_provider.SetMagnificationEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetMagnificationZoomFactor(
      10, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetScreenReaderEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetColorInversionEnabled(
      true, [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);
  settings_provider.SetColorCorrection(
      fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
      [&status](SettingsManagerStatus retval) { status = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, SettingsManagerStatus::OK);

  // Verify new settings in App.
  float kExpectedZoomFactor = 10.0;
  settings = app.GetSettings();
  EXPECT_TRUE(settings->has_magnification_enabled());
  EXPECT_TRUE(settings->magnification_enabled());
  EXPECT_TRUE(settings->has_magnification_zoom_factor());
  EXPECT_EQ(kExpectedZoomFactor, settings->magnification_zoom_factor());
  EXPECT_TRUE(settings->has_screen_reader_enabled());
  EXPECT_TRUE(settings->screen_reader_enabled());
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_TRUE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::CORRECT_PROTANOMALY,
            settings->color_correction());
  EXPECT_TRUE(settings->has_color_adjustment_matrix());
}

// This test makes sure that services implemented by the Tts manager are
// available.
TEST_F(AppUnitTest, OffersTtsManagerServices) {
  a11y_manager::App app = a11y_manager::App(context_provider_.TakeContext());
  RunLoopUntilIdle();
  fuchsia::accessibility::tts::TtsManagerPtr tts_manager;
  context_provider_.ConnectToPublicService(tts_manager.NewRequest());
  RunLoopUntilIdle();
  ASSERT_TRUE(tts_manager.is_bound());
}

TEST_F(AppUnitTest, NoListenerInitially) {
  MockPointerEventRegistry registry(&context_provider_);
  MockSetUIAccessibility setui(&context_provider_);
  a11y_manager::App app(context_provider_.TakeContext());

  setui.Set({}, [](auto) {});

  RunLoopUntilIdle();
  EXPECT_FALSE(registry.listener())
      << "No listener should be registered in the beginning, as there is no accessibility service "
         "enabled.";
}

TEST_F(AppUnitTest, ListenerForScreenReader) {
  MockPointerEventRegistry registry(&context_provider_);
  MockSetUIAccessibility setui(&context_provider_);
  a11y_manager::App app(context_provider_.TakeContext());
  EXPECT_FALSE(app.state().screen_reader_enabled());

  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  setui.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  EXPECT_TRUE(app.state().screen_reader_enabled());

  ASSERT_TRUE(registry.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&registry.listener()), EventHandling::CONSUMED);
}

TEST_F(AppUnitTest, ListenerForMagnifier) {
  MockPointerEventRegistry registry(&context_provider_);
  MockSetUIAccessibility setui(&context_provider_);
  a11y_manager::App app(context_provider_.TakeContext());

  fuchsia::settings::AccessibilitySettings settings;
  settings.set_enable_magnification(true);
  setui.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  ASSERT_TRUE(registry.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&registry.listener()), EventHandling::REJECTED);
}

TEST_F(AppUnitTest, ListenerForAll) {
  MockPointerEventRegistry registry(&context_provider_);
  MockSetUIAccessibility setui(&context_provider_);
  a11y_manager::App app(context_provider_.TakeContext());

  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  settings.set_enable_magnification(true);
  setui.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  ASSERT_TRUE(registry.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&registry.listener()), EventHandling::CONSUMED);
}

TEST_F(AppUnitTest, NoListenerAfterAllRemoved) {
  MockPointerEventRegistry registry(&context_provider_);
  MockSetUIAccessibility setui(&context_provider_);
  a11y_manager::App app(context_provider_.TakeContext());

  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  settings.set_enable_magnification(true);
  setui.Set(std::move(settings), [](auto) {});

  settings.set_screen_reader(false);
  settings.set_enable_magnification(false);
  setui.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  EXPECT_FALSE(registry.listener());
}

// Covers a couple additional edge cases around the listener ref count.
TEST_F(AppUnitTest, ListenerRefCount) {
  MockPointerEventRegistry registry(&context_provider_);
  MockSetUIAccessibility setui(&context_provider_);
  a11y_manager::App app(context_provider_.TakeContext());

  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  settings.set_enable_magnification(true);
  setui.Set(std::move(settings), [](auto) {});

  settings.set_screen_reader(false);
  settings.set_enable_magnification(true);
  setui.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();

  EXPECT_EQ(app.state().screen_reader_enabled(), false);
  EXPECT_EQ(app.state().magnifier_enabled(), true);

  ASSERT_TRUE(registry.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&registry.listener()), EventHandling::REJECTED);

  settings.set_enable_magnification(false);
  setui.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  EXPECT_FALSE(registry.listener());
}

// This test makes sure that the accessibility manager is watching for settings updates from setUI.
// TODO(17180): When we move away from a monolithic settings UI inside a11y manager this should test
// that configuration changes actually happen rather than just making sure bits get set.
TEST_F(AppUnitTest, WatchesSetUISettings) {
  // Create a mock setUI & configure initial settings (everything off).
  MockSetUIAccessibility mock_setui(&context_provider_);
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(false);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui.Set(std::move(accessibilitySettings), [](auto) {});
  a11y_manager::App app = a11y_manager::App(context_provider_.TakeContext());
  RunLoopUntilIdle();

  // Verify that app settings are initialized appropriately.
  SettingsPtr settings = app.GetSettings();
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_FALSE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::DISABLED, settings->color_correction());

  // Change the settings values (everything on).
  fuchsia::settings::AccessibilitySettings newAccessibilitySettings;
  newAccessibilitySettings.set_screen_reader(true);
  newAccessibilitySettings.set_color_inversion(true);
  newAccessibilitySettings.set_enable_magnification(true);
  newAccessibilitySettings.set_color_correction(
      fuchsia::settings::ColorBlindnessType::DEUTERANOMALY);
  mock_setui.Set(std::move(newAccessibilitySettings), [](auto) {});
  RunLoopUntilIdle();

  // Verify that stuff changed
  settings = app.GetSettings();
  EXPECT_TRUE(settings->has_color_inversion_enabled());
  EXPECT_TRUE(settings->color_inversion_enabled());
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::CORRECT_DEUTERANOMALY,
            settings->color_correction());
}

TEST_F(AppUnitTest, ColorCorrectionApplied) {
  // Create a mock color transform handler.
  MockColorTransformHandler mock_color_transform_handler(&context_provider_);

  // Create a mock setUI & configure initial settings (everything off).
  MockSetUIAccessibility mock_setui(&context_provider_);
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(false);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui.Set(std::move(accessibilitySettings), [](auto) {});
  a11y_manager::App app = a11y_manager::App(context_provider_.TakeContext());
  RunLoopUntilIdle();

  // Turn on color correction.
  fuchsia::settings::AccessibilitySettings newAccessibilitySettings;
  newAccessibilitySettings.set_color_correction(
      fuchsia::settings::ColorBlindnessType::DEUTERANOMALY);
  mock_setui.Set(std::move(newAccessibilitySettings), [](auto) {});
  RunLoopUntilIdle();

  // Verify that stuff changed
  SettingsPtr settings = app.GetSettings();
  EXPECT_TRUE(settings->has_color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrection::CORRECT_DEUTERANOMALY,
            settings->color_correction());
  EXPECT_EQ(fuchsia::accessibility::ColorCorrectionMode::CORRECT_DEUTERANOMALY,
            mock_color_transform_handler.GetColorCorrectionMode());
}

}  // namespace
}  // namespace accessibility_test
