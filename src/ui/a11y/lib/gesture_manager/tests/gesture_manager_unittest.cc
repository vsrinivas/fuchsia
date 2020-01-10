// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"

namespace a11y {
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

  GestureManager gesture_manager_;
  GestureHandler* gesture_handler_;
  fuchsia::ui::input::accessibility::PointerEventListenerPtr listener_;
};

void GestureManagerTest::SetUp() {
  listener_.Bind(gesture_manager_.binding().NewBinding());
  gesture_handler_ = gesture_manager_.gesture_handler();
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

TEST_F(GestureManagerTest, CallsActionOnSingleTap) {
  // Registers the callback (in a real use case, an Screen Reader action for example), that will be
  // invoked once a gesture is detected.
  zx_koid_t actual_viewref_koid = 0;
  fuchsia::math::PointF actual_point = {.x = 0, .y = 0};
  bool single_tap_detected = false;
  bool double_tap_detected = false;
  auto single_tap_callback = [&actual_viewref_koid, &actual_point, &single_tap_detected](
                                 zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    single_tap_detected = true;
  };
  auto double_tap_callback = [&actual_viewref_koid, &actual_point, &double_tap_detected](
                                 zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    double_tap_detected = true;
  };

  // Bind Gestures - Gesture with higher priority should be added first.
  gesture_handler_->BindOneFingerDoubleTapAction(std::move(double_tap_callback));
  gesture_handler_->BindOneFingerSingleTapAction(std::move(single_tap_callback));

  uint32_t actual_device_id = 0, actual_pointer_id = 1000;
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [&actual_device_id, &actual_pointer_id, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);
  ExecuteOneFingerTapAction(&listener_);
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_viewref_koid, kDefaultKoid);
  EXPECT_EQ(actual_point.x, kLocalPoint.x);
  EXPECT_EQ(actual_point.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_TRUE(single_tap_detected);
  EXPECT_FALSE(double_tap_detected);
}

TEST_F(GestureManagerTest, CallsActionOnDoubleTap) {
  // Registers the callback (in a real use case, an Screen Reader action for example), that will be
  // invoked once a gesture is detected.
  auto* gesture_handler = gesture_manager_.gesture_handler();
  zx_koid_t actual_viewref_koid = 0;
  fuchsia::math::PointF actual_point = {.x = 0, .y = 0};
  bool single_tap_detected = false;
  bool double_tap_detected = false;
  auto single_tap_callback = [&actual_viewref_koid, &actual_point, &single_tap_detected](
                                 zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    single_tap_detected = true;
  };
  auto double_tap_callback = [&actual_viewref_koid, &actual_point, &double_tap_detected](
                                 zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    double_tap_detected = true;
  };

  // Bind Gestures - Gesture with higher priority should be added first.
  gesture_handler->BindOneFingerDoubleTapAction(std::move(double_tap_callback));
  gesture_handler->BindOneFingerSingleTapAction(std::move(single_tap_callback));

  uint32_t actual_device_id = 0, actual_pointer_id = 1000;
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [&actual_device_id, &actual_pointer_id, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  ExecuteOneFingerTapAction(&listener_);
  ExecuteOneFingerTapAction(&listener_);
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_viewref_koid, kDefaultKoid);
  EXPECT_EQ(actual_point.x, kLocalPoint.x);
  EXPECT_EQ(actual_point.y, kLocalPoint.y);

  EXPECT_EQ(actual_device_id, kDefaultDeviceId);
  EXPECT_EQ(actual_pointer_id, kDefaultPointerId);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  EXPECT_TRUE(double_tap_detected);
  EXPECT_FALSE(single_tap_detected);
}

TEST_F(GestureManagerTest, NoGestureDetected) {
  // Registers the callback (in a real use case, an Screen Reader action for example), that will be
  // invoked once a gesture is detected.
  auto* gesture_handler = gesture_manager_.gesture_handler();
  zx_koid_t actual_viewref_koid = 0;
  fuchsia::math::PointF actual_point = {.x = 0, .y = 0};
  bool single_tap_detected = false;
  bool double_tap_detected = false;
  auto single_tap_callback = [&actual_viewref_koid, &actual_point, &single_tap_detected](
                                 zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    single_tap_detected = true;
  };
  auto double_tap_callback = [&actual_viewref_koid, &actual_point, &double_tap_detected](
                                 zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    double_tap_detected = true;
  };

  // Bind Gestures - Gesture with higher priority should be added first.
  gesture_handler->BindOneFingerDoubleTapAction(std::move(double_tap_callback));
  gesture_handler->BindOneFingerSingleTapAction(std::move(single_tap_callback));

  uint32_t actual_device_id = 0, actual_pointer_id = 1000;
  fuchsia::ui::input::accessibility::EventHandling actual_handled =
      fuchsia::ui::input::accessibility::EventHandling::REJECTED;

  auto listener_callback = [&actual_device_id, &actual_pointer_id, &actual_handled](
                               uint32_t device_id, uint32_t pointer_id,
                               fuchsia::ui::input::accessibility::EventHandling handled) {
    actual_device_id = device_id;
    actual_pointer_id = pointer_id;
    actual_handled = handled;
  };
  listener_.events().OnStreamHandled = std::move(listener_callback);

  // Send an ADD event.
  auto event = GetDefaultPointerEvent();
  listener_->OnEvent(std::move(event));
  RunLoopFor(a11y::OneFingerNTapRecognizer::kTapTimeout);

  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::REJECTED);
  EXPECT_FALSE(double_tap_detected);
  EXPECT_FALSE(single_tap_detected);
}

TEST_F(GestureManagerTest, BindGestureMultipleTimes) {
  // Registers the callback (in a real use case, an Screen Reader action for example), that will be
  // invoked once a gesture is detected.
  auto* gesture_handler = gesture_manager_.gesture_handler();
  zx_koid_t actual_viewref_koid = 0;
  fuchsia::math::PointF actual_point = {.x = 0, .y = 0};
  bool double_tap_detected = false;
  auto double_tap_callback_1 = [&actual_viewref_koid, &actual_point, &double_tap_detected](
                                   zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    double_tap_detected = true;
  };
  auto double_tap_callback_2 = [&actual_viewref_koid, &actual_point, &double_tap_detected](
                                   zx_koid_t viewref_koid, fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
    double_tap_detected = true;
  };

  EXPECT_TRUE(gesture_handler->BindOneFingerDoubleTapAction(std::move(double_tap_callback_1)));
  // Calling Bind again should fail since the gesture is already binded.
  EXPECT_FALSE(gesture_handler->BindOneFingerDoubleTapAction(std::move(double_tap_callback_2)));
}

}  // namespace
}  // namespace a11y
