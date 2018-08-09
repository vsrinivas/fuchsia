// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/bin/a11y/talkback/gesture_detector.h"
#include "garnet/bin/a11y/tests/mocks/mock_gesture_listener.h"
#include "garnet/bin/a11y/tests/mocks/mock_touch_dispatcher.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/component/cpp/testing/test_with_context.h"

namespace talkback_test {

// Unit tests for garnet/bin/a11y/talkback/gesture_detector.h
class GestureDetectorTest : public component::testing::TestWithContext {
 public:
  void SetUp() override {
    TestWithContext::SetUp();
    controller().AddService<fuchsia::accessibility::TouchDispatcher>(
        [this](fidl::InterfaceRequest<fuchsia::accessibility::TouchDispatcher>
                   request) { touch_dispatcher_.Bind(std::move(request)); });
    context_ = TakeContext();
    detector_ =
        std::make_unique<talkback::GestureDetector>(context_.get(), &listener_);
    RunLoopUntilIdle();
  }

  accessibility_test::MockTouchDispatcher touch_dispatcher_;
  MockGestureListener listener_;

 private:
  std::unique_ptr<component::StartupContext> context_;
  std::unique_ptr<talkback::GestureDetector> detector_;
};

TEST_F(GestureDetectorTest, TapTest) {
  uint32_t gesture_count = 0;
  listener_.SetCallback([&gesture_count](Gesture gesture) {
    EXPECT_EQ(gesture, Gesture::kTap);
    gesture_count++;
  });

  // Touch down
  fuchsia::ui::input::PointerEvent touch;
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  touch.event_time = 0;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch move
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = 100;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch up
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::UP;
  touch.event_time = 200;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Wait until tap is registered after delay
  RunLoopFor(zx::msec(200));
  EXPECT_EQ(1u, gesture_count);
}

TEST_F(GestureDetectorTest, DoubleTapTest) {
  uint32_t gesture_count = 0;
  listener_.SetCallback([&gesture_count](Gesture gesture) {
    EXPECT_EQ(gesture, Gesture::kDoubleTap);
    gesture_count++;
  });

  // Touch down
  fuchsia::ui::input::PointerEvent touch;
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  touch.event_time = 0;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch move
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = 100;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch up
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::UP;
  touch.event_time = 200;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Second touch down
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  touch.event_time = 300;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Second touch move
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = 400;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Second touch up
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::UP;
  touch.event_time = 500;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Make sure a delayed tap does not happen because a double tap
  // was registered.
  RunLoopFor(zx::msec(200));
  EXPECT_EQ(1u, gesture_count);
}

TEST_F(GestureDetectorTest, MoveTest) {
  uint32_t gesture_count = 0;
  listener_.SetCallback([&gesture_count](Gesture gesture) {
    // Talkback needs to do a query from a11y manager on a move.
    EXPECT_EQ(gesture, Gesture::kMove);
    gesture_count++;
  });

  // Touch down
  fuchsia::ui::input::PointerEvent touch;
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  touch.event_time = 0;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch move after 100 ns; a touch move should not register.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = 100;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch move after 130 ms; a touch move should register.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = (uint64_t)zx::msec(130).to_nsecs();
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch move after 140 ms; a touch move should not register.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = (uint64_t)zx::msec(140).to_nsecs();
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Touch move after 150 ms; a touch move should register.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = (uint64_t)zx::msec(150).to_nsecs();
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  RunLoopUntilIdle();
  EXPECT_EQ(2u, gesture_count);
}

TEST_F(GestureDetectorTest, TwoFingerTest) {
  uint32_t simulated_touch_count = 0;
  // Finger #1 pointer_id = 0; finger #2 pointer_id = 1;
  touch_dispatcher_.callback_ =
      [&simulated_touch_count](fuchsia::ui::input::PointerEvent event) {
        // We simulate events for finger #1 back to touch dispatcher.
        EXPECT_EQ(0u, event.pointer_id);
        simulated_touch_count++;
      };
  // Finger #1 touch down;
  fuchsia::ui::input::PointerEvent touch;
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  touch.event_time = 0;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Finger #2 touch down; simulated ADD/DOWN events fired.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  touch.event_time = 100;
  touch.pointer_id = 1;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Finger #1 move; simulated MOVE event.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = 200;
  touch.pointer_id = 0;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Finger #2 move; simulated MOVE event.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
  touch.event_time = 300;
  touch.pointer_id = 1;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  // Second finger lift up; simulated UP/REMOVE events.
  touch.type = fuchsia::ui::input::PointerEventType::TOUCH;
  touch.phase = fuchsia::ui::input::PointerEventPhase::UP;
  touch.event_time = 400;
  touch.pointer_id = 1;
  touch_dispatcher_.SendPointerEventToClient(std::move(touch));

  RunLoopUntilIdle();
  // Expect 5 simulated events, touch ADD, DOWN, MOVE, UP, REMOVE
  EXPECT_EQ(5u, simulated_touch_count);
}

}  // namespace talkback_test
