// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/ui/a11y/lib/gesture_manager/recognizers/m_finger_n_tap_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/two_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {
namespace {

constexpr uint32_t kDefaultKoid = 100;
constexpr fuchsia::math::PointF kLocalPoint = {2, 2};
constexpr uint32_t kDefaultEventTime = 10;
constexpr uint32_t kDefaultDeviceId = 1;
constexpr uint32_t kDefaultPointerId = 1;

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

class GestureManagerTest : public gtest::TestLoopFixture {
 public:
  GestureManagerTest() { SetUp(); }
  void SetUp() override;
  ~GestureManagerTest() override = default;

  a11y::GestureManager gesture_manager_;
  a11y::GestureHandler* gesture_handler_;
  fuchsia::ui::input::accessibility::PointerEventListenerPtr listener_;
  bool one_finger_up_swipe_detected_ = false;
  bool one_finger_down_swipe_detected_ = false;
  bool one_finger_left_swipe_detected_ = false;
  bool one_finger_right_swipe_detected_ = false;
  bool three_finger_up_swipe_detected_ = false;
  bool three_finger_down_swipe_detected_ = false;
  bool three_finger_left_swipe_detected_ = false;
  bool three_finger_right_swipe_detected_ = false;
  bool single_tap_detected_ = false;
  bool double_tap_detected_ = false;
  bool one_finger_drag_detected_ = false;
  bool two_finger_drag_detected_ = false;
  bool one_finger_triple_tap_detected_ = false;
  bool one_finger_triple_tap_drag_detected_ = false;
  bool three_finger_double_tap_detected_ = false;
  bool three_finger_double_tap_drag_detected_ = false;
  zx_koid_t actual_viewref_koid_ = 0;
  fuchsia::math::PointF actual_point_ = {.x = 0, .y = 0};
  uint32_t actual_device_id_ = 0;
  uint32_t actual_pointer_id_ = 1000;
};

void GestureManagerTest::SetUp() {
  listener_.Bind(gesture_manager_.binding().NewBinding());
  gesture_handler_ = gesture_manager_.gesture_handler();

  auto one_finger_up_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    one_finger_up_swipe_detected_ = true;
  };

  auto one_finger_down_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    one_finger_down_swipe_detected_ = true;
  };

  auto one_finger_left_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    one_finger_left_swipe_detected_ = true;
  };

  auto one_finger_right_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    one_finger_right_swipe_detected_ = true;
  };

  auto three_finger_up_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    three_finger_up_swipe_detected_ = true;
  };

  auto three_finger_down_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    three_finger_down_swipe_detected_ = true;
  };

  auto three_finger_left_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    three_finger_left_swipe_detected_ = true;
  };

  auto three_finger_right_swipe_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    three_finger_right_swipe_detected_ = true;
  };

  auto single_tap_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    single_tap_detected_ = true;
  };
  auto double_tap_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    double_tap_detected_ = true;
  };

  auto one_finger_triple_tap_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    one_finger_triple_tap_detected_ = true;
  };

  auto one_finger_triple_tap_drag_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    one_finger_triple_tap_drag_detected_ = true;
  };

  auto three_finger_double_tap_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    three_finger_double_tap_detected_ = true;
  };

  auto three_finger_double_tap_drag_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    three_finger_double_tap_drag_detected_ = true;
  };

  auto one_finger_drag_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    one_finger_drag_detected_ = true;
  };

  auto two_finger_drag_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    two_finger_drag_detected_ = true;
  };

  // Bind Gestures - Gesture with higher priority should be added first.

  gesture_handler_->BindMFingerNTapAction(1, 3, std::move(one_finger_triple_tap_callback));
  gesture_handler_->BindMFingerNTapAction(3, 2, std::move(three_finger_double_tap_callback));
  gesture_handler_->BindMFingerNTapDragAction(
      std::move(one_finger_triple_tap_drag_callback), [](a11y::GestureContext context) {},
      [](a11y::GestureContext context) {}, 1, 3);
  gesture_handler_->BindMFingerNTapDragAction(
      std::move(three_finger_double_tap_drag_callback), [](a11y::GestureContext context) {},
      [](a11y::GestureContext context) {}, 3, 2);
  gesture_handler_->BindTwoFingerDragAction(
      std::move(two_finger_drag_callback), [](a11y::GestureContext context) {},
      [](a11y::GestureContext context) {});
  gesture_handler_->BindSwipeAction(std::move(one_finger_up_swipe_callback),
                                    a11y::GestureHandler::kOneFingerUpSwipe);
  gesture_handler_->BindSwipeAction(std::move(one_finger_down_swipe_callback),
                                    a11y::GestureHandler::kOneFingerDownSwipe);
  gesture_handler_->BindSwipeAction(std::move(one_finger_left_swipe_callback),
                                    a11y::GestureHandler::kOneFingerLeftSwipe);
  gesture_handler_->BindSwipeAction(std::move(one_finger_right_swipe_callback),
                                    a11y::GestureHandler::kOneFingerRightSwipe);
  gesture_handler_->BindSwipeAction(std::move(three_finger_up_swipe_callback),
                                    a11y::GestureHandler::kThreeFingerUpSwipe);
  gesture_handler_->BindSwipeAction(std::move(three_finger_down_swipe_callback),
                                    a11y::GestureHandler::kThreeFingerDownSwipe);
  gesture_handler_->BindSwipeAction(std::move(three_finger_left_swipe_callback),
                                    a11y::GestureHandler::kThreeFingerLeftSwipe);
  gesture_handler_->BindSwipeAction(std::move(three_finger_right_swipe_callback),
                                    a11y::GestureHandler::kThreeFingerRightSwipe);
  gesture_handler_->BindOneFingerDoubleTapAction(std::move(double_tap_callback));
  gesture_handler_->BindOneFingerSingleTapAction(std::move(single_tap_callback));
  gesture_handler_->BindOneFingerDragAction(
      std::move(one_finger_drag_callback), [](a11y::GestureContext context) {},
      [](a11y::GestureContext context) {});
}

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(kDefaultEventTime);
  event.set_device_id(kDefaultDeviceId);
  event.set_pointer_id(kDefaultPointerId);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(Phase::ADD);
  event.set_ndc_point({4, 4});
  event.set_viewref_koid(kDefaultKoid);
  event.set_local_point(kLocalPoint);
  return event;
}

void ExecuteOneFingerTapAction(
    fuchsia::ui::input::accessibility::PointerEventListenerPtr* listener) {
  {
    // Send an ADD event.
    auto event = GetDefaultPointerEvent();
    (*listener)->OnEvent(std::move(event));
  }
  {
    // Send a Down event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    (*listener)->OnEvent(std::move(event));
  }
  {
    // Send an UP event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    (*listener)->OnEvent(std::move(event));
  }
  {
    // Send a REMOVE event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    (*listener)->OnEvent(std::move(event));
  }
}

void SendPointerEvents(fuchsia::ui::input::accessibility::PointerEventListenerPtr* listener,
                       const std::vector<PointerParams>& events) {
  for (const auto& event : events) {
    auto pointer_event = ToPointerEvent(event, 0 /* event time (unused) */);
    pointer_event.set_device_id(kDefaultDeviceId);
    // pointer_event.set_pointer_id(kDefaultPointerId);
    pointer_event.set_viewref_koid(kDefaultKoid);
    pointer_event.set_local_point(kLocalPoint);
    (*listener)->OnEvent(std::move(pointer_event));
  }
}

TEST_F(GestureManagerTest, CallsActionOnSingleTap) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  ExecuteOneFingerTapAction(&listener_);
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_TRUE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnDoubleTap) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  ExecuteOneFingerTapAction(&listener_);
  ExecuteOneFingerTapAction(&listener_);
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_TRUE(double_tap_detected_);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnOneFingerUpSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  // Perform Up Swipe.
  glm::vec2 first_update_ndc_position = {0, -.7f};
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));

  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_TRUE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnThreeFingerUpSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  // Perform Three finger Up Swipe.
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, MoveEvents(finger, {}, {0, -.7f}));
  }
  // UP event must be between .375 and .75 NDC from DOWN event for gesture to be considered
  // a swipe.
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, UpEvents(finger, {0, -.7f}));
  }

  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_TRUE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnOneFingerDownSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  glm::vec2 first_update_ndc_position = {0, .7f};

  // Perform Down Swipe.
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_TRUE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnThreeFingerDownSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  // Perform Three Finger Down Swipe.
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, MoveEvents(finger, {}, {0, .7f}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, UpEvents(finger, {0, .7f}));
  }
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_TRUE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnOneFingerLeftSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  glm::vec2 first_update_ndc_position = {-.7f, 0};

  // Perform Left Swipe.
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_TRUE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnThreeFingerLeftSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  // Perform Three Finger Left Swipe.
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, MoveEvents(finger, {}, {-.7f, 0}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, UpEvents(finger, {-.7f, 0}));
  }
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_TRUE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnOneFingerRightSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  glm::vec2 first_update_ndc_position = {.7f, 0};

  // Perform Right Swipe.
  SendPointerEvents(
      &listener_, DownEvents(kDefaultPointerId, {}) + MoveEvents(1, {}, first_update_ndc_position));
  SendPointerEvents(&listener_, MoveEvents(kDefaultPointerId, first_update_ndc_position,
                                           first_update_ndc_position, 1) +
                                    UpEvents(kDefaultPointerId, first_update_ndc_position));
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id_, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_TRUE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnThreeFingerRightSwipe) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  // Perform Three Finger Right Swipe.
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, MoveEvents(finger, {}, {.7f, 0}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, UpEvents(finger, {.7f, 0}));
  }
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_TRUE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnOneFingerTripleTap) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  ExecuteOneFingerTapAction(&listener_);
  ExecuteOneFingerTapAction(&listener_);
  ExecuteOneFingerTapAction(&listener_);
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_TRUE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnOneFingerTripleTapDrag) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  ExecuteOneFingerTapAction(&listener_);
  ExecuteOneFingerTapAction(&listener_);
  auto event = GetDefaultPointerEvent();
  event.set_pointer_id(kDefaultPointerId);
  event.set_phase(Phase::DOWN);
  listener_->OnEvent(std::move(event));
  RunLoopFor(a11y::MFingerNTapDragRecognizer::kMinTapHoldDuration);

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_TRUE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, NoGestureDetected) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  // Send an ADD event.
  auto event = GetDefaultPointerEvent();
  listener_->OnEvent(std::move(event));
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnThreeFingerDoubleTap) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, UpEvents(finger, {.7f, 0}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, UpEvents(finger, {.7f, 0}));
  }
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_TRUE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnThreeFingerDoubleTapDrag) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, UpEvents(finger, {.7f, 0}));
  }
  for (uint32_t finger = 0; finger < 3; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }
  RunLoopFor(a11y::MFingerNTapDragRecognizer::kMinTapHoldDuration);
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_FALSE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_TRUE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, CallsActionOnTwoFingerDrag) {
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [this, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id_ = device_id;
    actual_pointer_id_ = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  for (uint32_t finger = 0; finger < 2; finger++) {
    SendPointerEvents(&listener_, DownEvents(finger, {}));
  }

  RunLoopFor(a11y::TwoFingerDragRecognizer::kDefaultMinDragDuration);
  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid_, kDefaultKoid);
  EXPECT_EQ(actual_point_.x, kLocalPoint.x);
  EXPECT_EQ(actual_point_.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id_, kDefaultDeviceId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_FALSE(single_tap_detected_);
  EXPECT_FALSE(double_tap_detected_);
  EXPECT_FALSE(one_finger_up_swipe_detected_);
  EXPECT_FALSE(one_finger_down_swipe_detected_);
  EXPECT_FALSE(one_finger_left_swipe_detected_);
  EXPECT_FALSE(one_finger_right_swipe_detected_);
  EXPECT_FALSE(three_finger_up_swipe_detected_);
  EXPECT_FALSE(three_finger_down_swipe_detected_);
  EXPECT_FALSE(three_finger_left_swipe_detected_);
  EXPECT_FALSE(three_finger_right_swipe_detected_);
  EXPECT_FALSE(one_finger_drag_detected_);
  EXPECT_TRUE(two_finger_drag_detected_);
  EXPECT_FALSE(one_finger_triple_tap_detected_);
  EXPECT_FALSE(one_finger_triple_tap_drag_detected_);
  EXPECT_FALSE(three_finger_double_tap_detected_);
  EXPECT_FALSE(three_finger_double_tap_drag_detected_);
}

TEST_F(GestureManagerTest, BindGestureMultipleTimes) {
  auto double_tap_callback = [this](a11y::GestureContext context) {
    actual_viewref_koid_ = context.view_ref_koid;
    actual_point_ = context.CurrentCentroid(true);
    double_tap_detected_ = true;
  };
  // Calling Bind again should fail since the gesture is already binded.
  EXPECT_FALSE(gesture_handler_->BindOneFingerDoubleTapAction(std::move(double_tap_callback)));
}

}  // namespace
}  // namespace accessibility_test
