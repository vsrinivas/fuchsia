// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_HANDLER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_HANDLER_H_

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <zircon/types.h>

#include <optional>

namespace a11y {

// A GestureHandler binds gestures to actions, and offers a way to call the actions bound to an
// action later.
class GestureHandler {
 public:
  // The high-level gestures identified by this class.
  // TODO(lucasradaelli): Implement time-based gestures (such as double taps).
  // TODO(lucasradaelli): Implement swipe-like gestures.
  // TODO(lucasradaelli): Implement multiple finger taps.
  enum GestureType { kUnknown, kOneFingerTap };

  // Some gestures need additional information about what was touched and where it was touched on
  // the screen. Callers of OnGesture() may provide this information.
  // TODO(lucasradaelli): Expand this arguments to support multi-pointer IDs.
  struct GestureArguments {
    // Viewref koid of the view where the gesture was performed.
    std::optional<zx_koid_t> viewref_koid;
    // Local view coordinate when the pointer ID on a DOWN event.
    // TODO(lucasradaelli): Implement multi-pointer ID coordinate storage. Right now, only one
    // finger tap is implemented, so this is enough here.
    std::optional<fuchsia::math::PointF> coordinates;
  };
  // TODO(lucasradaelli): Decide whether to implement a single callback for all gestures or a
  // per-gesture callback.
  using OneFingerTapCallback = fit::function<void(zx_koid_t, fuchsia::math::PointF)>;

  GestureHandler() = default;
  ~GestureHandler() = default;

  // Binds the action defined in |callback| with the gesture |kOneFingerTap|.
  void BindOneFingerTapAction(OneFingerTapCallback callback) {
    one_finger_tap_callback_ = std::move(callback);
  }

  // Calls an action bound to |gesture_type| if it exists and returns true, false otherwise.
  bool OnGesture(GestureType gesture_type, GestureArguments args);

 private:
  OneFingerTapCallback one_finger_tap_callback_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_HANDLER_H_
