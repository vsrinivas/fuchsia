// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_UTIL_UTIL_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_UTIL_UTIL_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/async/cpp/task.h"

namespace a11y {

// Struct for holding context(Koid, location) about Gesture.
struct GestureContext {
  zx_koid_t view_ref_koid;
  std::optional<::fuchsia::math::PointF> local_point;
};

// Struct for holding initial information about Gesture under consideration.
struct GestureInfo {
  uint64_t gesture_start_time;
  uint32_t device_id;
  uint32_t pointer_id;
  ::fuchsia::math::PointF starting_global_position;
  std::optional<::fuchsia::math::PointF> starting_local_position;
  zx_koid_t view_ref_koid;
  uint64_t single_tap_detected_time;
  bool is_winner_ = false;
};

// Callback which will be used while scheduling a task. When the scheduled task will run, it will
// invoke this callback.
using ScheduleTaskCallback =
    fit::function<void(async_dispatcher_t* dispatcher, async::Task* task, zx_status_t status)>;

// Helper function to schedule a task with a timeout. After the timeout is complete provided
// callback function will be called.
void ScheduleCallbackTask(async::Task* gesture_task, ScheduleTaskCallback callback,
                          const uint32_t one_finger_tap_timeout);

// Helper function to cancel the task scheduled by ScheduleCallbackTask.
void CancelCallbackTask(async::Task* gesture_task);

// Helper function to initialize GestureInfo and GestureContext using the provided pointer_event.
// This function returns falls when pointer_event is missing required fields like pointer id,
// global_point, device_id and event_time.
// If all the above are present, GestureInfo and GestureContext are initialized and function returns
// true.
bool InitGestureInfo(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event,
                     GestureInfo* gesture_start_info, GestureContext* gesture_context);

// Helper function to check if essential fields(like event time, device id, pointer id and global
// point) are present in the pointer event for the current gesture. It also makes sure that device
// id and pointer id has not changed for the gesture.
bool ValidatePointerEvent(const GestureInfo gesture_start_info,
                          const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_UTIL_UTIL_H_
