// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>

#include "fuchsia/accessibility/gesture/cpp/fidl.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_listener_registry.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/swipe_recognizer_base.h"
#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_handler.h"
#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_listener.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree_service_factory.h"
#include "src/ui/a11y/lib/testing/input.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using GestureType = a11y::GestureHandler::GestureType;
using PointerEventPhase = fuchsia::ui::input::PointerEventPhase;
using fuchsia::accessibility::gesture::Type;
using fuchsia::accessibility::semantics::Node;
using Phase = fuchsia::ui::input::PointerEventPhase;
using testing::ElementsAre;
using testing::StrEq;

class MockScreenReaderActionRegistryImpl : public a11y::ScreenReaderActionRegistry,
                                           public a11y::ScreenReaderAction {
 public:
  MockScreenReaderActionRegistryImpl() = default;
  ~MockScreenReaderActionRegistryImpl() override = default;
  void AddAction(std::string name, std::unique_ptr<ScreenReaderAction> action) override {
    actions_.insert(std::move(name));
  }

  ScreenReaderAction* GetActionByName(const std::string& name) override {
    auto action_it = actions_.find(name);
    if (action_it == actions_.end()) {
      return nullptr;
    }
    invoked_actions_.push_back(name);
    return this;
  }

  void Run(ActionData process_data) override {}

  std::vector<std::string>& invoked_actions() { return invoked_actions_; }

 private:
  std::unordered_set<std::string> actions_;
  std::vector<std::string> invoked_actions_;
};

class ScreenReaderTest : public gtest::TestLoopFixture {
 public:
  ScreenReaderTest()
      : factory_(std::make_unique<MockSemanticTreeServiceFactory>()),
        factory_ptr_(factory_.get()),
        context_provider_(),
        view_manager_(std::move(factory_), std::make_unique<MockViewSemanticsFactory>(),
                      std::make_unique<MockAnnotationViewFactory>(), context_provider_.context(),
                      context_provider_.context()->outgoing()->debug_dir()),
        context_(std::make_unique<MockScreenReaderContext>()),
        context_ptr_(context_.get()),
        a11y_focus_manager_ptr_(context_ptr_->mock_a11y_focus_manager_ptr()),
        mock_speaker_ptr_(context_ptr_->mock_speaker_ptr()),
        mock_action_registry_(std::make_unique<MockScreenReaderActionRegistryImpl>()),
        mock_action_registry_ptr_(mock_action_registry_.get()),
        screen_reader_(std::move(context_), &view_manager_, &gesture_listener_registry_,
                       std::move(mock_action_registry_)),
        semantic_provider_(&view_manager_) {
    screen_reader_.BindGestures(&mock_gesture_handler_);
    gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

    semantic_provider_.SetSemanticsEnabled(true);
    view_manager_.SetSemanticsEnabled(true);
    factory_ptr_->service()->EnableSemanticsUpdates(true);
  }

  std::unique_ptr<MockSemanticTreeServiceFactory> factory_;
  MockSemanticTreeServiceFactory* factory_ptr_;
  sys::testing::ComponentContextProvider context_provider_;
  a11y::ViewManager view_manager_;
  a11y::GestureManager gesture_manager_;
  a11y::GestureListenerRegistry gesture_listener_registry_;
  MockGestureListener mock_gesture_listener_;
  MockGestureHandler mock_gesture_handler_;

  std::unique_ptr<MockScreenReaderContext> context_;
  MockScreenReaderContext* context_ptr_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  MockScreenReaderContext::MockSpeaker* mock_speaker_ptr_;
  std::unique_ptr<MockScreenReaderActionRegistryImpl> mock_action_registry_;
  MockScreenReaderActionRegistryImpl* mock_action_registry_ptr_;
  a11y::ScreenReader screen_reader_;
  MockSemanticProvider semantic_provider_;
};  // namespace

TEST_F(ScreenReaderTest, GestureHandlersAreRegisteredIntheRightOrder) {
  // The order in which the Screen Reader registers the gesture handlers at startup is relevant.
  // Each registered handler is saved in the mock, so we can check if they are in the right order
  // here.
  EXPECT_THAT(mock_gesture_handler_.bound_gestures(),
              ElementsAre(GestureType::kThreeFingerUpSwipe, GestureType::kThreeFingerDownSwipe,
                          GestureType::kThreeFingerLeftSwipe, GestureType::kThreeFingerRightSwipe,
                          GestureType::kOneFingerDownSwipe, GestureType::kOneFingerUpSwipe,
                          GestureType::kOneFingerLeftSwipe, GestureType::kOneFingerRightSwipe,
                          GestureType::kOneFingerDoubleTap, GestureType::kOneFingerSingleTap,
                          GestureType::kOneFingerDrag, GestureType::kTwoFingerSingleTap));
}

TEST_F(ScreenReaderTest, RegisteredActionsAreInvokedWhenGestureTriggers) {
  mock_gesture_handler_.TriggerGesture(
      GestureType::kThreeFingerUpSwipe);  // corresponds to physical right.
  mock_gesture_handler_.TriggerGesture(
      GestureType::kThreeFingerDownSwipe);  // Corresponds to physical left.
  mock_gesture_handler_.TriggerGesture(
      GestureType::kThreeFingerLeftSwipe);  // Corresponds to physical up
  mock_gesture_handler_.TriggerGesture(
      GestureType::kThreeFingerRightSwipe);  // Corresponds to physical down.
  mock_gesture_handler_.TriggerGesture(
      GestureType::kOneFingerUpSwipe);  // Corresponds to physical right.
  mock_gesture_handler_.TriggerGesture(
      GestureType::kOneFingerDownSwipe);  // Corresponds to physical left.
  mock_gesture_handler_.TriggerGesture(
      GestureType::kOneFingerLeftSwipe);  // Corresponds to a physical up.
  mock_gesture_handler_.TriggerGesture(
      GestureType::kOneFingerRightSwipe);  // Corresponds to a physical down.
  mock_gesture_handler_.TriggerGesture(GestureType::kOneFingerDoubleTap);
  // Note that since one finger single tap and drag both trigger the explore action, we expect to
  // see it twice in the list of called actions.
  mock_gesture_handler_.TriggerGesture(GestureType::kOneFingerSingleTap);
  mock_gesture_handler_.TriggerGesture(GestureType::kOneFingerDrag);
  RunLoopUntilIdle();
  EXPECT_THAT(
      mock_action_registry_ptr_->invoked_actions(),
      ElementsAre(StrEq("Three finger Right Swipe Action"), StrEq("Three finger Left Swipe Action"),
                  StrEq("Three finger Up Swipe Action"), StrEq("Three finger Down Swipe Action"),
                  StrEq("Next Action"), StrEq("Previous Action"),
                  StrEq("Previous Semantic Level Action"), StrEq("Next Semantic Level Action"),
                  StrEq("Default Action"), StrEq("Explore Action"), StrEq("Explore Action")));
}

TEST_F(ScreenReaderTest, TrivialActionsAreInvokedWhenGestureTriggers) {
  // Trivial actions are not registered in the action registry, but are jusst the callback parked at
  // the gesture handler. Verify that the results of the callback are seen when it runs.
  mock_gesture_handler_.TriggerGesture(GestureType::kTwoFingerSingleTap);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedCancel());
}

}  // namespace
}  // namespace accessibility_test
