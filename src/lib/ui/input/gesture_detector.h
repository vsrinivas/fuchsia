// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_UI_INPUT_GESTURE_DETECTOR_H_
#define SRC_LIB_UI_INPUT_GESTURE_DETECTOR_H_

#include <fuchsia/ui/input/cpp/fidl.h>

#include <map>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/ui/input/gesture.h"
#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace input {

// A basic higher-level gesture classifier. This gesture detector classifies
// gestures along two dimensions: "tap type" (number of touch points, mouse
// buttons, or stylus usage) and tap vs. drag. See |Interaction| for details.
//
// Gestures are handled independently for each input device.
class GestureDetector {
 public:
  // This default tends to be reasonable if input units are in pixels.
  static constexpr float kDefaultDragThreshold = 8;

  // Tap type, in terms of number of pointers used, or mouse button.
  //
  // Roughly, 1 can be considered the primary interaction mode (one finger,
  // primary mouse button, or stylus touch), 2 can be considered secondary (two
  // fingers, secondary mouse button, or stylus button/inverted touch), and so
  // on.
  //
  // TODO(fxbug.dev/17287): time-based taps
  using TapType = int32_t;

  // Represents a single gesture interaction. This is a higher level event-based
  // abstraction of |Gesture| that adds tap classification (provided by
  // |GestureDetector|). Subclasses override methods (and the destructor) to
  // handle events. The default implementation does nothing.
  //
  // Broadly, this handles two classes of gestures: taps and (multi)drags. Taps
  // are defined as the addition of touch points and their subsequent removal
  // without appreciable movement from any of the pointers. Multidrags cover
  // all other cases.
  // TODO(fxbug.dev/17287): This may need redefinition if time-based taps are
  // supported at this level.
  //
  // In the interest of reducing input latency, a multi-pointer tap is
  // considered "committed" as soon as any pointer is released. However, it can
  // subsequently evolve into a multidrag if any remaining pointer is moved
  // past the drag threshold or any new pointers are added.
  //
  // The |Interaction| is destroyed once the interaction ends, when all
  // pointers are removed.
  // TODO(fxbug.dev/17287): This may happen after a timeout once time-based taps are
  // supported.
  class Interaction {
   public:
    virtual ~Interaction();

    // Called when the first pointer comes down.
    virtual void OnTapBegin(const glm::vec2& coordinate, TapType tap_type);
    // Called when the type of tap has changed, either due to more mouse buttons
    // or more touch points. As a tap evolves, |tap_type| can increase but not
    // decrease, as any release signifies the end of the tap.
    virtual void OnTapUpdate(TapType tap_type);
    // Called when any pointer involved in a tap comes up. At this point, the
    // tap is considered "committed". However, it may subsequently evolve into a
    // multidrag if any remaining pointer is moved past the drag threshold or
    // any new pointers are added.
    //
    // TODO(fxbug.dev/18121, fxbug.dev/17287): This definition may change significantly in the face
    // of pluggable tap classification.
    virtual void OnTapCommit();

    // Called for a multipoint drag gesture. Unlike in a tap gesture, |tap_type|
    // here may decrease as fingers are removed or as buttons are released. Once
    // this is called, this interaction is no longer considered a tap, and no
    // subsequent tap-related methods will be called.
    virtual void OnMultidrag(TapType tap_type, const Gesture::Delta& delta);
  };

  // Constructs |Interaction| subclass instances that will represent
  // interactions detected by a |GestureDetector|.
  class Delegate {
   public:
    virtual ~Delegate();

    // Factory function for |Interaction| implementations. |gesture| is the
    // underlying gesture and will outlive the interaction.
    virtual std::unique_ptr<Interaction> BeginInteraction(const Gesture* gesture) = 0;
  };

  // If any touch point deviates |drag_threshold| from its origin, the input is
  // considered a multidrag.
  GestureDetector(Delegate* delegate, float drag_threshold = kDefaultDragThreshold);

  void OnPointerEvent(fuchsia::ui::input::PointerEvent event);

  // Clears all tracked devices and interactions from this detector.
  void Reset() { devices_.clear(); }

 private:
  using DeviceId = uint32_t;

  struct DevicePointerState {
    DevicePointerState();

    Gesture gesture;
    std::unique_ptr<Interaction> interaction;
    std::map<Gesture::PointerId, glm::vec2> origins;
    // While an interaction can be classified as a tap, this tracks the tap
    // type, > 0, nondecreasing. Once a tap is committed, this becomes negative.
    // If this interaction becomes a multidrag, this is set to 0. Only positive
    // tap types are ever exposed to the client.
    //
    // Example sequences:
    // 1 -> 2 -> -2: two-finger tap
    // 1 -> 0: one-finger drag
    // 1 -> 2 -> -2 -> 0: two-finger tap, committed (one released), subsequently
    //   dragged
    TapType tap_type;
    Gesture::Delta pending_delta;

    // The delegate implementation may choose to reset or destroy the |GestureDetector| in any of
    // its methods, so any time we call out to the delegate, we should guard subsequent execution
    // with a weak pointer to the state.
    fxl::WeakPtr<DevicePointerState> GetWeakPtr();

   private:
    fxl::WeakPtrFactory<DevicePointerState> weak_ptr_factory_;
  };

  Delegate* const delegate_;
  std::map<DeviceId, DevicePointerState> devices_;
  float drag_threshold_squared_;
};

}  // namespace input

#endif  // SRC_LIB_UI_INPUT_GESTURE_DETECTOR_H_
