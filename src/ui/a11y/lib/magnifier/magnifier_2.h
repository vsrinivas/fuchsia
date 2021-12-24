// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_MAGNIFIER_MAGNIFIER_2_H_
#define SRC_UI_A11Y_LIB_MAGNIFIER_MAGNIFIER_2_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>

#include "src/lib/callback/scoped_task_runner.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"
#include "src/ui/a11y/lib/magnifier/magnifier_util.h"

#include <glm/glm.hpp>

namespace a11y {

class Magnifier2 : public fuchsia::accessibility::Magnifier {
 public:
  // Transition over .2 s @ 60 fps.
  static constexpr zx::duration kTransitionPeriod = zx::msec(200);
  static constexpr float kTransitionRate = 1 / (kTransitionPeriod.to_msecs() * .060f);
  static constexpr float kDragThreshold = 1.f / 16;  // NDC
  static constexpr float kMinScale = 1, kMaxScale = 20;
  static constexpr float kDefaultScale = 4;

  Magnifier2();
  ~Magnifier2() override = default;

  // |fuchsia::accessibility::Magnifier|
  void RegisterHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) override;

  // Method to register recognizers in a gesture recognition arena.
  void BindGestures(a11y::GestureHandler* gesture_handler);

  // Returns the screen to "normal" zoom (scale = 1) if it's currently magnified.
  void ZoomOutIfMagnified();

 private:
  // Indicates magnification mode.
  // TODO(fxb/69736): Mention that there are more details in README doc.
  enum class Mode {
    // In the unmagnified state, the magnifier is at "normal zoom" and is
    // unresponsive to two-finger drags.
    UNMAGNIFIED = 0,

    // The temporary magnification mode is activated by a
    // one-finger-triple-tap-drag or a three-finger-double-tap-drag.
    // The screen is magnified to the default scale (kDefaultScale),
    // and returns to normal zoom (scale = 1) once the finger(s) are lifted.
    // The magnifier is not responive to two-finger drags in temporary
    // magnification mode.
    TEMPORARY = 1,

    // The persistent magnification mode is activated by a one-finger-triple-tap
    // or a three-finger-double-tap. The screen remains magnified until the next
    // tap gesture explicity returns the magnifier to the unmagnified mode.
    // The magnifier is responsive to two-finger drags in persistent
    // magnification mode.
    PERSISTENT = 2
  };

  struct State {
    // Indicates current mode of magnification.
    Mode mode = Mode::UNMAGNIFIED;

    // Indicates current pointer locations (if a gesture has been recognized and
    // is still in progress).
    // This state is necessary to enable us to compute changes in magnification
    // scale/translation.
    GestureContext gesture_context;

    float transition_rate = 0;
    float scale = kDefaultScale;
    glm::vec2 translation = {0, 0};

    bool operator==(const State& o) const;
    bool operator!=(const State& o) const;

    // Helper that sets the magnified translation to focus on the given screen coordinate. This does
    // not call |UpdateTransform|.
    void FocusOn(const ::fuchsia::math::PointF& focus);

    // True if a call to SetClipSpaceTransform() is in progress, and we are
    // waiting on a response from scenic.
    // We need to maintain this state in order to avoid requesting multiple clip
    // space transform updates before the first call returns.
    bool update_in_progress = false;

    // True if the clip space transform requires further updates.
    // This state is used to help animate "smooth" transitions between different
    // zoom levels.
    bool update_pending = false;

    // When we transition from one zoom/focus to another, we update the clip
    // space transform incrementally to animate a "smooth" transition (e.g.
    // instead of changing the scale directly from A->B, we change it from
    // A -> (A + delta) -> (A + 2*delta) -> ... -> B).
    //
    // |transition_progress_| is a float between 0 and 1, and it's used to
    // compute the transform at some intermediate point during the transition
    // between two zoom/focus states.
    float transition_progress = 0;

    // Indicates whether to draw the viewport highlight.
    // This state is necessary to avoid a race condition when transitioning out
    // of zoom where we clear highlights before the transition is complete, in
    // which case we would re-draw the magnification highlight (and it would
    // never be cleared).
    bool draw_highlight = false;
  };

  // Transitions from unmagnified to magnified at kDefaultScale.
  void TransitionIntoZoom();

  // Transitions from magnified to unmagnified.
  void TransitionOutOfZoom();

  // Sends the updated transform to the handler.
  void UpdateTransform();

  // Toggles persistent magnification on/off.
  void TogglePersistentMagnification();

  // Updates magnification transform to reflect the state of an in-progress
  // drag during temporary magnification.
  void HandleTemporaryDrag(const Delta& delta);

  // Updates magnification transform to reflect the state of an in-progress
  // drag during persistent magnification.
  // NOTE: Do NOT update state_.gesture_context prior to calling this method, as
  // it requires the "old" gesture context.
  void HandlePersistentDrag(const Delta& delta);

  // Ensures that the translation falls within the allowable range (as dictated
  // by the min/max scale).
  void ClampTranslation();

  // Magnifier state.
  State state_;

  fuchsia::accessibility::MagnificationHandlerPtr handler_;

  callback::ScopedTaskRunner handler_scope_;
};

}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_MAGNIFIER_2_H_
