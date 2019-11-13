// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>

#include <memory>
#include <vector>

namespace a11y {
namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

using GestureManagerTest = gtest::TestLoopFixture;

// Returns a default Accessibility Pointer Event.
AccessibilityPointerEvent GetDefaultPointerEvent() {
  AccessibilityPointerEvent event;
  event.set_event_time(10);
  event.set_device_id(1);
  event.set_pointer_id(1);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(Phase::ADD);
  event.set_ndc_point({4, 4});
  event.set_viewref_koid(100);
  event.set_local_point({2, 2});
  return event;
}

TEST_F(GestureManagerTest, CallsActionOnTouch) {
  GestureManager gesture_manager;
  fuchsia::ui::input::accessibility::PointerEventListenerPtr listener;
  listener.Bind(gesture_manager.binding().NewBinding());
  // Registers the callback (in a real use case, an Screen Reader action for example), that will be
  // invoked once a gesture is detected. For now, this only detects a single touch (finger down ->
  // up).
  auto* gesture_handler = gesture_manager.gesture_handler();
  zx_koid_t actual_viewref_koid = 0;
  fuchsia::math::PointF actual_point = {.x = 0, .y = 0};
  auto callback = [&actual_viewref_koid, &actual_point](zx_koid_t viewref_koid,
                                                        fuchsia::math::PointF point) {
    actual_viewref_koid = viewref_koid;
    actual_point = point;
  };
  gesture_handler->BindOneFingerTapAction(std::move(callback));

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
  listener.events().OnStreamHandled = std::move(listener_callback);

  {
    // Sends an ADD event.
    auto event = GetDefaultPointerEvent();
    listener->OnEvent(std::move(event));
  }

  {
    // Down event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::DOWN);
    listener->OnEvent(std::move(event));
  }

  {
    // UP event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::UP);
    listener->OnEvent(std::move(event));
  }

  {
    // REMOVE event.
    auto event = GetDefaultPointerEvent();
    event.set_phase(Phase::REMOVE);
    listener->OnEvent(std::move(event));
  }

  RunLoopUntilIdle();

  EXPECT_EQ(actual_viewref_koid, 100u);
  EXPECT_EQ(actual_point.x, 2);
  EXPECT_EQ(actual_point.y, 2);

  EXPECT_EQ(actual_device_id, 1u);
  EXPECT_EQ(actual_pointer_id, 1u);
  EXPECT_EQ(actual_handled, fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
}
}  // namespace
}  // namespace a11y
