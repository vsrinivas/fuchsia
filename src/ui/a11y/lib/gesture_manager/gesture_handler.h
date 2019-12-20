// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_HANDLER_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_HANDLER_H_

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <unordered_map>

#include "src/ui/a11y/lib/gesture_manager/arena/gesture_arena.h"
#include "src/ui/a11y/lib/gesture_manager/arena/recognizer.h"

namespace a11y {

// A GestureHandler binds gestures to actions, and allows gestures to call these
// actions when necessary.
class GestureHandler {
 public:
  // Callback which will be used to add recognizers to gesture arena.
  using AddRecognizerToArenaCallback = fit::function<void(GestureRecognizer*)>;

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
  // Callback invoked when the gesture it is bound to is detected.
  using OnGestureCallback = fit::function<void(zx_koid_t, fuchsia::math::PointF)>;

  explicit GestureHandler(AddRecognizerToArenaCallback add_recognizer_callback);
  ~GestureHandler() = default;

  // Binds the action defined in |callback| with the gesture |kOneFingerTap|.
  void BindOneFingerTapAction(OnGestureCallback callback);

  // Binds a recognizer that consumes everything.
  void ConsumeAll();

 private:
  // Calls an action bound to |gesture_type| if it exists and returns true, false otherwise.
  bool OnGesture(GestureType gesture_type, GestureArguments args);

  // Action of kOneFingerTap.
  OnGestureCallback one_finger_tap_callback_;

  // Callback to add recognizer to gesture arena.
  AddRecognizerToArenaCallback add_recognizer_callback_;

  // As callbacks are added to the handler to be invoked when a gesture is
  // performed, the recognizers capable of identifying them are instantiated and
  // stored here.
  std::unordered_map<GestureType, std::unique_ptr<GestureRecognizer>> gesture_recognizers_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_HANDLER_H_
