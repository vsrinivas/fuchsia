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
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_focus_chain.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_pointer_event_registry.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_setui_accessibility.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnification_handler.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
using fuchsia::accessibility::semantics::Role;
using fuchsia::ui::input::accessibility::EventHandling;
using fuchsia::ui::input::accessibility::PointerEventListener;
using fuchsia::ui::input::accessibility::PointerEventListenerPtr;

class AppUnitTest : public gtest::TestLoopFixture {
 public:
  AppUnitTest()
      : context_provider_(),
        context_(context_provider_.context()),
        mock_pointer_event_registry_(&context_provider_),
        mock_color_transform_handler_(&context_provider_),
        mock_setui_(&context_provider_),
        mock_focus_chain_(&context_provider_),
        view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      context_->outgoing()->debug_dir()),
        tts_manager_(context_),
        color_transform_manager_(context_),
        app_(context_, &view_manager_, &tts_manager_, &color_transform_manager_) {}
  void SetUp() override {
    TestLoopFixture::SetUp();

    zx::eventpair::create(0u, &eventpair_, &eventpair_peer_);
    view_ref_ = fuchsia::ui::views::ViewRef({
        .reference = std::move(eventpair_),
    });
    RunLoopUntilIdle();
  }

  // Sends pointer events and returns the |handled| argument of the (last) resulting
  // |OnStreamHandled| invocation.
  //
  // Yo dawg, I heard you like pointer event listener pointers, so I took a pointer to your pointer
  // event listener pointer so you can receive events while you receive events (while honoring the
  // C++ style guide).
  std::optional<EventHandling> SendPointerEvents(PointerEventListenerPtr* listener,
                                                 const std::vector<PointerParams>& events) {
    std::optional<EventHandling> event_handling;
    listener->events().OnStreamHandled =
        [&event_handling](uint32_t, uint32_t, EventHandling handled) { event_handling = handled; };

    for (const auto& params : events) {
      SendPointerEvent(listener->get(), params);
    }

    return event_handling;
  }

  void SendPointerEvent(PointerEventListener* listener, const PointerParams& params) {
    listener->OnEvent(ToPointerEvent(params, input_event_time_++));

    // Simulate trivial passage of time (can expose edge cases with posted async tasks).
    RunLoopUntilIdle();
  }

  // Sends a gesture that wouldn't be recognized by any accessibility feature, for testing arena
  // configuration.
  std::optional<EventHandling> SendUnrecognizedGesture(PointerEventListenerPtr* listener) {
    return SendPointerEvents(listener, Zip({TapEvents(1, {}), TapEvents(2, {})}));
  }

  sys::testing::ComponentContextProvider context_provider_;
  sys::ComponentContext* context_;

  MockPointerEventRegistry mock_pointer_event_registry_;
  MockColorTransformHandler mock_color_transform_handler_;
  MockSetUIAccessibility mock_setui_;
  MockFocusChain mock_focus_chain_;

  a11y::ViewManager view_manager_;
  a11y::TtsManager tts_manager_;
  a11y::ColorTransformManager color_transform_manager_;

  // App under test
  a11y_manager::App app_;

  fuchsia::ui::views::ViewRef view_ref_;
  zx::eventpair eventpair_, eventpair_peer_;

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

// Test to make sure ViewManager Service is exposed by A11y.
// Test sends a node update to ViewManager and then compare the expected
// result using log file created by semantics manager.
TEST_F(AppUnitTest, UpdateNodeToSemanticsManager) {
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

  // Check that the node is in the semantic tree
  auto tree = view_manager_.GetTreeByKoid(a11y::GetKoid(view_ref_));
  ASSERT_EQ(tree->GetNode(0)->attributes().label(), "Label A");

  // Check that the committed node is present in the logs
  vfs::PseudoDir* debug_dir = context_->outgoing()->debug_dir();
  vfs::internal::Node* test_node;
  ASSERT_EQ(ZX_OK, debug_dir->Lookup(std::to_string(a11y::GetKoid(view_ref_)), &test_node));
}

// This test makes sure that services implemented by the Tts manager are
// available.
TEST_F(AppUnitTest, OffersTtsManagerServices) {
  fuchsia::accessibility::tts::TtsManagerPtr tts_manager;
  context_provider_.ConnectToPublicService(tts_manager.NewRequest());
  RunLoopUntilIdle();
  ASSERT_TRUE(tts_manager.is_bound());
}

TEST_F(AppUnitTest, NoListenerInitially) {
  mock_setui_.Set({}, [](auto) {});

  RunLoopUntilIdle();
  EXPECT_FALSE(mock_pointer_event_registry_.listener())
      << "No listener should be registered in the beginning, as there is no accessibility service "
         "enabled.";
}

TEST_F(AppUnitTest, ListenerForScreenReader) {
  EXPECT_FALSE(app_.state().screen_reader_enabled());

  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  mock_setui_.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  EXPECT_TRUE(app_.state().screen_reader_enabled());

  ASSERT_TRUE(mock_pointer_event_registry_.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&mock_pointer_event_registry_.listener()),
            EventHandling::CONSUMED);
}

TEST_F(AppUnitTest, ListenerForMagnifier) {
  fuchsia::settings::AccessibilitySettings settings;
  settings.set_enable_magnification(true);
  mock_setui_.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  EXPECT_TRUE(app_.state().magnifier_enabled());

  ASSERT_TRUE(mock_pointer_event_registry_.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&mock_pointer_event_registry_.listener()),
            EventHandling::REJECTED);
}

TEST_F(AppUnitTest, ListenerForAll) {
  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  settings.set_enable_magnification(true);
  mock_setui_.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  ASSERT_TRUE(mock_pointer_event_registry_.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&mock_pointer_event_registry_.listener()),
            EventHandling::CONSUMED);
}

TEST_F(AppUnitTest, NoListenerAfterAllRemoved) {
  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  settings.set_enable_magnification(true);
  mock_setui_.Set(std::move(settings), [](auto) {});
  RunLoopUntilIdle();

  settings.set_screen_reader(false);
  settings.set_enable_magnification(false);
  mock_setui_.Set(std::move(settings), [](auto) {});

  RunLoopUntilIdle();
  EXPECT_FALSE(mock_pointer_event_registry_.listener());
}

// Covers a couple additional edge cases around removing listeners.
TEST_F(AppUnitTest, ListenerRemoveOneByOne) {
  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  settings.set_enable_magnification(true);
  mock_setui_.Set(std::move(settings), [](auto) {});
  RunLoopUntilIdle();

  settings.set_screen_reader(false);
  settings.set_enable_magnification(true);
  mock_setui_.Set(std::move(settings), [](auto) {});
  RunLoopUntilIdle();

  EXPECT_EQ(app_.state().screen_reader_enabled(), false);
  EXPECT_EQ(app_.state().magnifier_enabled(), true);

  ASSERT_TRUE(mock_pointer_event_registry_.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&mock_pointer_event_registry_.listener()),
            EventHandling::REJECTED);

  settings.set_enable_magnification(false);
  mock_setui_.Set(std::move(settings), [](auto) {});
  RunLoopUntilIdle();

  EXPECT_EQ(app_.state().magnifier_enabled(), false);
  EXPECT_FALSE(mock_pointer_event_registry_.listener());
}

// Makes sure gesture priorities are right. If they're not, screen reader would intercept this
// gesture.
TEST_F(AppUnitTest, MagnifierGestureWithScreenReader) {
  MockMagnificationHandler mag_handler;
  fidl::Binding<fuchsia::accessibility::MagnificationHandler> mag_handler_binding(&mag_handler);
  {
    fuchsia::accessibility::MagnifierPtr magnifier;
    context_provider_.ConnectToPublicService(magnifier.NewRequest());
    magnifier->RegisterHandler(mag_handler_binding.NewBinding());
  }

  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  settings.set_enable_magnification(true);
  mock_setui_.Set(std::move(settings), [](auto) {});
  RunLoopUntilIdle();

  SendPointerEvents(&mock_pointer_event_registry_.listener(), 3 * TapEvents(1, {}));
  RunLoopFor(a11y::Magnifier::kTransitionPeriod);

  EXPECT_GT(mag_handler.transform().scale, 1);
}

TEST_F(AppUnitTest, ColorCorrectionApplied) {
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(false);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui_.Set(std::move(accessibilitySettings), [](auto) {});

  RunLoopUntilIdle();

  EXPECT_EQ(fuchsia::accessibility::ColorCorrectionMode::DISABLED,
            mock_color_transform_handler_.GetColorCorrectionMode());

  // Turn on color correction.
  fuchsia::settings::AccessibilitySettings newAccessibilitySettings;
  newAccessibilitySettings.set_color_correction(
      fuchsia::settings::ColorBlindnessType::DEUTERANOMALY);
  mock_setui_.Set(std::move(newAccessibilitySettings), [](auto) {});
  RunLoopUntilIdle();

  // Verify that stuff changed
  EXPECT_EQ(fuchsia::accessibility::ColorCorrectionMode::CORRECT_DEUTERANOMALY,
            mock_color_transform_handler_.GetColorCorrectionMode());
}

TEST_F(AppUnitTest, ColorInversionApplied) {
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(false);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui_.Set(std::move(accessibilitySettings), [](auto) {});
  RunLoopUntilIdle();

  EXPECT_FALSE(mock_color_transform_handler_.GetColorInversionEnabled());

  // Turn on color correction.
  fuchsia::settings::AccessibilitySettings newAccessibilitySettings;
  newAccessibilitySettings.set_color_inversion(true);
  mock_setui_.Set(std::move(newAccessibilitySettings), [](auto) {});
  RunLoopUntilIdle();

  // Verify that stuff changed
  EXPECT_TRUE(mock_color_transform_handler_.GetColorInversionEnabled());
}

TEST_F(AppUnitTest, ScreenReaderOnAtStartup) {
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(true);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui_.Set(std::move(accessibilitySettings), [](auto) {});
  RunLoopUntilIdle();

  // Verify that screen reader is on and the pointer event registry is wired up.
  EXPECT_TRUE(app_.state().screen_reader_enabled());
  ASSERT_TRUE(mock_pointer_event_registry_.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&mock_pointer_event_registry_.listener()),
            EventHandling::CONSUMED);
}

TEST_F(AppUnitTest, InitializesFocusChain) {
  // Ensures that when App is initialized, it connects to the Focus Chain different services.
  RunLoopUntilIdle();

  ASSERT_TRUE(mock_focus_chain_.listener());
  ASSERT_TRUE(mock_focus_chain_.HasRegisteredFocuser());
}

// TODO(fxb/48064): Write a test to verify that screen reader is wired up with the Focus Chain when
// it initializes.

}  // namespace
}  // namespace accessibility_test
