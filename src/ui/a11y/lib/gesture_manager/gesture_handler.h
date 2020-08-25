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
  enum GestureType {
    kUnknown,
    kOneFingerSingleTap,
    kOneFingerDoubleTap,
    kOneFingerDrag,
    kOneFingerUpSwipe,
    kOneFingerDownSwipe,
    kOneFingerLeftSwipe,
    kOneFingerRightSwipe,
    kThreeFingerUpSwipe,
    kThreeFingerDownSwipe,
    kThreeFingerLeftSwipe,
    kThreeFingerRightSwipe,
    kTwoFingerSingleTap,
  };

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
  virtual ~GestureHandler() = default;

  // Binds the action defined in |callback| with the gesture |kOneFingerSingleTap|. The action is
  // invoked only after the gesture is detected.
  virtual bool BindOneFingerSingleTapAction(OnGestureCallback callback);

  // Binds the action defined in |callback| with the gesture |kOneFingerDoubleTap|. The action is
  // invoked only after the gesture is detected.
  virtual bool BindOneFingerDoubleTapAction(OnGestureCallback callback);

  // Binds the actions with the gesture |kOneFingerDrag|.
  // They are called when the drag starts, updates and completes, respectively.
  virtual bool BindOneFingerDragAction(OnGestureCallback on_start, OnGestureCallback on_update,
                                       OnGestureCallback on_complete);

  // Binds the action defined in |callback| with the |gesture_type|. Returns true if the |callback|
  // is bound, false otherwise
  virtual bool BindSwipeAction(OnGestureCallback callback, GestureType gesture_type);

  // Binds the action defined in |callback| with the |kTwoFingerSingleTap|. The
  // action is invoked only after the gesture is detected.
  virtual bool BindTwoFingerSingleTapAction(OnGestureCallback callback);

  // Binds a recognizer that consumes everything.
  void ConsumeAll();

 protected:
  // Constructor for mocks.
  GestureHandler() = default;

  // The handlers for each gesture recognizer event.
  struct GestureEventHandlers {
    OnGestureCallback on_start;
    OnGestureCallback on_update;
    OnGestureCallback on_complete;
  };

  enum class GestureEvent { kStart, kUpdate, kComplete };

 private:
  // Calls an action bound to |gesture_type| if it exists.
  void OnGesture(const GestureType gesture_type, const GestureEvent gesture_event,
                 GestureArguments args);

  // Helper function to bind the action defined in |callback| with the tap recognizer where taps
  // equal to |num_of_taps| if no action is currently binded for the given |gesture_type|. Returns
  // true if the |callback| is bound, false otherwise.
  bool BindOneFingerNTapAction(OnGestureCallback callback, int number_of_taps);

  // Helper function to bind the action defined in |callback| with the gesture Up swipe gesture with
  // |gesture_type| if no action is currently binded. Returns true if the |callback| is bound,
  // false otherwise.
  bool BindUpSwipeAction(OnGestureCallback callback, GestureType gesture_type);

  // Helper function to bind the action defined in |callback| with the gesture Down swipe gesture
  // with |gesture_type| if no action is currently binded. Returns true if the |callback| is
  // bound, false otherwise.
  bool BindDownSwipeAction(OnGestureCallback callback, GestureType gesture_type);

  // Helper function to bind the action defined in |callback| with the gesture Left swipe gesture
  // with |gesture_type| if no action is currently binded. Returns true if the |callback| is
  // bound, false otherwise.
  bool BindLeftSwipeAction(OnGestureCallback callback, GestureType gesture_type);

  // Helper function to bind the action defined in |callback| with the gesture Right swipe gesture
  // with |gesture_type| if no action is currently binded. Returns true if the |callback| is
  // bound, false otherwise.
  bool BindRightSwipeAction(OnGestureCallback callback, GestureType gesture_type);

  // Callback to add recognizer to gesture arena.
  AddRecognizerToArenaCallback add_recognizer_callback_;

  // Map to store callback associated with the gesture.
  std::unordered_map<GestureType, GestureEventHandlers> gesture_handlers_;

  // As callbacks are added to the handler to be invoked when a gesture is
  // performed, the recognizers capable of identifying them are instantiated and
  // stored here.
  std::unordered_map<GestureType, std::unique_ptr<GestureRecognizer>> gesture_recognizers_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_HANDLER_H_
