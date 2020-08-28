// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/app.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>

#include <gtest/gtest.h>

#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_color_transform_handler.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_focus_chain.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_pointer_event_registry.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_property_provider.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_setui_accessibility.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnification_handler.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
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
        mock_property_provider_(&context_provider_),
        mock_annotation_view_factory_(new MockAnnotationViewFactory()),
        view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                      std::make_unique<MockViewSemanticsFactory>(),
                      std::unique_ptr<MockAnnotationViewFactory>(mock_annotation_view_factory_),
                      context_provider_.context(), context_->outgoing()->debug_dir()),
        tts_manager_(context_),
        color_transform_manager_(context_),
        app_(context_, &view_manager_, &tts_manager_, &color_transform_manager_,
             &gesture_listener_registry_) {}

  void SetUp() override {
    TestLoopFixture::SetUp();
    RunLoopUntilIdle();
    // App is created, but is not fully-initialized.  Make sure the fetch of settings only happens
    // after it has been initialized.
    EXPECT_EQ(0, mock_setui_.num_watch_called());
    // Right now, obtaining the locale causes the app to be fully-initialized.
    ASSERT_EQ(1, mock_property_provider_.get_profile_count());
    mock_property_provider_.SetLocale("en");
    mock_property_provider_.ReplyToGetProfile();
    RunLoopUntilIdle();
    ASSERT_EQ(1,
              mock_property_provider_.get_profile_count());  // Stil 1, no changes in profile yet.
    // Note: 2 here because as soon as we get a settings, we call Watch() again.
    ASSERT_EQ(2, mock_setui_.num_watch_called());

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
    listener->events().OnStreamHandled = [&event_handling](uint32_t /*unused*/, uint32_t /*unused*/,
                                                           EventHandling handled) {
      event_handling = handled;
    };

    for (const auto& params : events) {
      SendPointerEvent(listener->get(), params);
    }

    return event_handling;
  }

  void SendPointerEvent(PointerEventListener* listener, const PointerParams& params) {
    listener->OnEvent(ToPointerEvent(params, input_event_time_++, a11y::GetKoid(view_ref_)));

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
  MockPropertyProvider mock_property_provider_;
  MockAnnotationViewFactory* mock_annotation_view_factory_;

  a11y::ViewManager view_manager_;
  a11y::TtsManager tts_manager_;
  a11y::ColorTransformManager color_transform_manager_;
  a11y::GestureListenerRegistry gesture_listener_registry_;

  // App under test
  a11y_manager::App app_;

  fuchsia::ui::views::ViewRef view_ref_;
  zx::eventpair eventpair_, eventpair_peer_;

 private:
  // We don't actually use these times. If we did, we'd want to more closely correlate them with
  // fake time.
  uint64_t input_event_time_ = 0;
};

// Test to make sure ViewManager Service is exposed by A11y.
// Test sends a node update to ViewManager and then compare the expected
// result using log file created by semantics manager.
TEST_F(AppUnitTest, UpdateNodeToSemanticsManager) {
  // Create ViewRef.
  fuchsia::ui::views::ViewRef view_ref_connection;
  fidl::Clone(view_ref_, &view_ref_connection);

  // Turn on the screen reader.
  fuchsia::settings::AccessibilitySettings settings;
  settings.set_screen_reader(true);
  mock_setui_.Set(std::move(settings), [](auto) {});

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
  auto created_node = view_manager_.GetSemanticNode(a11y::GetKoid(view_ref_), 0u);
  EXPECT_TRUE(created_node);
  EXPECT_EQ(created_node->attributes().label(), "Label A");

  // Check that the committed node is present in the logs
  vfs::PseudoDir* debug_dir = context_->outgoing()->debug_dir();
  vfs::internal::Node* test_node;
  EXPECT_EQ(ZX_OK, debug_dir->Lookup(std::to_string(a11y::GetKoid(view_ref_)), &test_node));
}

// This test makes sure that services implemented by the Tts manager are
// available.
TEST_F(AppUnitTest, OffersTtsManagerServices) {
  fuchsia::accessibility::tts::TtsManagerPtr tts_manager;
  context_provider_.ConnectToPublicService(tts_manager.NewRequest());
  RunLoopUntilIdle();
  EXPECT_TRUE(tts_manager.is_bound());
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
  {
    fuchsia::settings::AccessibilitySettings settings;
    settings.set_screen_reader(true);
    settings.set_enable_magnification(true);
    mock_setui_.Set(std::move(settings), [](auto) {});
  }
  RunLoopUntilIdle();
  {
    fuchsia::settings::AccessibilitySettings settings;
    settings.set_screen_reader(false);
    settings.set_enable_magnification(false);
    mock_setui_.Set(std::move(settings), [](auto) {});
  }

  RunLoopUntilIdle();
  EXPECT_FALSE(mock_pointer_event_registry_.listener());
}

// Covers a couple additional edge cases around removing listeners.
TEST_F(AppUnitTest, ListenerRemoveOneByOne) {
  {
    fuchsia::settings::AccessibilitySettings settings;
    settings.set_screen_reader(true);
    settings.set_enable_magnification(true);
    mock_setui_.Set(std::move(settings), [](auto) {});
  }
  RunLoopUntilIdle();
  {
    fuchsia::settings::AccessibilitySettings settings;
    settings.set_screen_reader(false);
    settings.set_enable_magnification(true);
    mock_setui_.Set(std::move(settings), [](auto) {});
  }
  RunLoopUntilIdle();

  EXPECT_EQ(app_.state().screen_reader_enabled(), false);
  EXPECT_EQ(app_.state().magnifier_enabled(), true);

  ASSERT_TRUE(mock_pointer_event_registry_.listener());
  EXPECT_EQ(SendUnrecognizedGesture(&mock_pointer_event_registry_.listener()),
            EventHandling::REJECTED);
  {
    fuchsia::settings::AccessibilitySettings settings;
    settings.set_screen_reader(false);
    settings.set_enable_magnification(false);
    mock_setui_.Set(std::move(settings), [](auto) {});
  }
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

// Makes sure FocusChain is wired up with the screen reader, when screen reader is enabled.
// This test uses explore action to make sure when a node is tapped, then screen reader can call
// RequestFocus() on FocusChain. This confirms that FocusChain is connected to ScreenReader.
TEST_F(AppUnitTest, FocusChainIsWiredToScreenReader) {
  // Enable Screen Reader.
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(true);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui_.Set(std::move(accessibilitySettings), [](auto) {});
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
  uint32_t node_id = 0;
  std::string node_label = "Label A";
  Node node = CreateTestNode(node_id, node_label);
  update_nodes.push_back(std::move(node));

  // Update the node created above.
  semantic_listener.UpdateSemanticNodes(std::move(update_nodes));
  RunLoopUntilIdle();

  // Commit nodes.
  semantic_listener.CommitUpdates();
  RunLoopUntilIdle();

  // Set HitTest result which is required to know which node is being tapped.
  semantic_listener.SetHitTestResult(node_id);

  // Send Tap event for view_ref_. This should trigger explore action, which should then call
  // FocusChain to set focus to the tapped view.
  SendPointerEvents(&mock_pointer_event_registry_.listener(), TapEvents(1, {}));
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  ASSERT_TRUE(mock_focus_chain_.IsRequestFocusCalled());
  EXPECT_EQ(a11y::GetKoid(view_ref_), mock_focus_chain_.GetFocusedViewKoid());

  auto highlighted_view =
      mock_annotation_view_factory_->GetAnnotationView(a11y::GetKoid(view_ref_));
  EXPECT_TRUE(highlighted_view);
  auto highlight = highlighted_view->GetCurrentHighlight();
  EXPECT_TRUE(highlight.has_value());
}

TEST_F(AppUnitTest, FetchesLocaleInfoOnStartup) {
  // App is initialized, so it should have requested once the locale.
  ASSERT_EQ(1, mock_property_provider_.get_profile_count());
  mock_property_provider_.SetLocale("en-US");
  mock_property_provider_.SendOnChangeEvent();
  RunLoopUntilIdle();
  // The event causes GetProfile() to be invoked again from the a11y manager side. Check if the call
  // happened through the mock.
  ASSERT_EQ(2, mock_property_provider_.get_profile_count());
}

TEST_F(AppUnitTest, ScreenReaderReceivesLocaleWhenItChanges) {
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(true);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui_.Set(std::move(accessibilitySettings), [](auto) {});
  RunLoopUntilIdle();
  EXPECT_TRUE(app_.state().screen_reader_enabled());
  EXPECT_EQ(app_.screen_reader()->context()->locale_id(), "en");
  mock_property_provider_.SetLocale("en-US");
  mock_property_provider_.SendOnChangeEvent();
  RunLoopUntilIdle();
  // The event causes GetProfile() to be invoked again from the a11y manager side. Check if the call
  // happened through the mock.
  ASSERT_EQ(2, mock_property_provider_.get_profile_count());
  // Sends a reply.
  mock_property_provider_.ReplyToGetProfile();
  RunLoopUntilIdle();
  EXPECT_EQ(app_.screen_reader()->context()->locale_id(), "en-US");
}

TEST_F(AppUnitTest, ScreenReaderUsesDefaultLocaleIfPropertyProviderDisconnectsOrIsNotAvailable) {
  EXPECT_FALSE(app_.state().screen_reader_enabled());
  mock_property_provider_.CloseChannels();
  RunLoopUntilIdle();
  // Only one call to GetProfile happened, because the channel was closed.
  ASSERT_EQ(1, mock_property_provider_.get_profile_count());
  // Turns on the Screen Reader and checks that it picks up the default locale.
  fuchsia::settings::AccessibilitySettings accessibilitySettings;
  accessibilitySettings.set_screen_reader(true);
  accessibilitySettings.set_color_inversion(false);
  accessibilitySettings.set_enable_magnification(false);
  accessibilitySettings.set_color_correction(fuchsia::settings::ColorBlindnessType::NONE);
  mock_setui_.Set(std::move(accessibilitySettings), [](auto) {});
  RunLoopUntilIdle();
  EXPECT_EQ(app_.screen_reader()->context()->locale_id(), "en-US");
}

// TODO(fxbug.dev/49924): Improve tests to cover what happens if services aren't available at
// startup.

}  // namespace
}  // namespace accessibility_test
