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
#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace a11y {

class Magnifier2 : public fuchsia::accessibility::Magnifier {
 public:
  // Transition over .2 s @ 60 fps.
  static constexpr zx::duration kTransitionPeriod = zx::msec(200);
  static constexpr float kTransitionRate = 1 / (kTransitionPeriod.to_msecs() * .060f);
  static constexpr float kDragThreshold = 1.f / 16;  // NDC
  static constexpr float kMinScale = 1, kMaxScale = 20;
  static constexpr float kDefaultScale = 4;

  Magnifier2() = default;
  ~Magnifier2() override = default;

  // |fuchsia::accessibility::Magnifier|
  void RegisterHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) override;

  // Method to register recognizers in a gesture recognition arena.
  void BindGestures(a11y::GestureHandler* gesture_handler);

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

    // The permanent magnification mode is activated by a one-finger-triple-tap
    // or a three-finger-double-tap. The screen remains magnified until the next
    // tap gesture explicity returns the magnifier to the unmagnified mode.
    // The magnifier is responsive to two-finger drags in permanent
    // magnification mode.
    PERMANENT = 2
  };

  struct State {
    // Indicates current mode of magnification.
    Mode mode;

    // Indicates current pointer locations (if a gesture has been recognized and
    // is still in progress).
    // This state is necessary to enable us to compute changes in magnification
    // scale/translation.
    GestureContext gesture_context;

    float transition_rate = 0;
    float magnified_scale = kDefaultScale;
    glm::vec2 magnified_translation = {0, 0};

    bool operator==(const State& o) const;
    bool operator!=(const State& o) const;

    // Helper that sets the magnified translation to focus on the given screen coordinate. This does
    // not call |UpdateTransform|.
    void FocusOn(const glm::vec2& focus);

    // True if a call to SetClipSpaceTransform() is in progress, and we are
    // waiting on a response from scenic.
    // We need to maintain this state in order to avoid requesting multiple clip
    // space transform updates before the first call returns.
    bool update_in_progress;

    // True if the clip space transform requires further updates.
    // This state is used to help animate "smooth" transitions between different
    // zoom levels.
    bool update_pending;

    // When we transition from one zoom/focus to another, we update the clip
    // space transform incrementally to animate a "smooth" transition (e.g.
    // instead of changing the scale directly from A->B, we change it from
    // A -> (A + delta) -> (A + 2*delta) -> ... -> B).
    //
    // |transition_progress_| is a float between 0 and 1, and it's used to
    // compute the transform at some intermediate point during the transition
    // between two zoom/focus states.
    float transition_progress;
  };

  // Magnifier state.
  State state_;

  fuchsia::accessibility::MagnificationHandlerPtr handler_;

  callback::ScopedTaskRunner handler_scope_;
};

}  // namespace a11y
#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_MAGNIFIER_2_H_
