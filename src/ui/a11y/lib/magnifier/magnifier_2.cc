// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/magnifier_2.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

Magnifier2::Magnifier2() {}

void Magnifier2::RegisterHandler(
    fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) {
  handler_scope_.Reset();
  state_.update_in_progress = state_.update_pending = false;
  handler_ = handler.Bind();
  // TODO(fxb/69730): Update transform here.
}

void Magnifier2::ZoomOutIfMagnified() {
  if (state_.mode != Mode::UNMAGNIFIED) {
    state_.mode = Mode::UNMAGNIFIED;
    TransitionOutOfZoom();
  }
}
bool Magnifier2::State::operator==(const State& o) const {
  return transition_rate == o.transition_rate && scale == o.scale && translation == o.translation;
}

bool Magnifier2::State::operator!=(const State& o) const { return !(*this == o); }

void Magnifier2::State::FocusOn(const ::fuchsia::math::PointF& focus) {
  glm::vec2 focus_vec;
  focus_vec.x = focus.x;
  focus_vec.y = focus.y;

  translation = -focus_vec * (scale - 1);
}

void Magnifier2::TransitionIntoZoom() {
  state_.FocusOn(state_.gesture_context.CurrentCentroid(false /* use_local_coordinates */));
  state_.scale = kDefaultScale;
  state_.transition_rate = kTransitionRate;
  UpdateTransform();
}

void Magnifier2::TransitionOutOfZoom() {
  state_.transition_rate = -kTransitionRate;
  UpdateTransform();
}

void Magnifier2::ClampTranslation() {
  auto& translation = state_.translation;
  const auto freedom = state_.scale - 1;
  translation.x = std::clamp(translation.x, -freedom, freedom);
  translation.y = std::clamp(translation.y, -freedom, freedom);
}

void Magnifier2::HandleTemporaryDrag(const Delta& delta) {
  FX_DCHECK(state_.mode == Mode::TEMPORARY);
  // If the zoom is temporary, treat the coordinate as a focal point, i.e.
  // focus on the area that would be at that position unzoomed.
  //
  // Instead of using the raw centroid coordinate, which jumps around as
  // fingers are added or removed, move the original tap coordinate by the
  // delta.
  state_.FocusOn(state_.gesture_context.CurrentCentroid(false /* use_local_coordinates */));

  // Ensure that translation does not fall outside of sensical values.
  ClampTranslation();

  UpdateTransform();
}

void Magnifier2::HandlePersistentDrag(const Delta& delta) {
  FX_DCHECK(state_.mode == Mode::PERSISTENT);

  float& scale = state_.scale;
  const float old_scale = scale;
  scale *= delta.scale;
  scale = std::clamp(scale, kMinScale, kMaxScale);
  // Account for clamping for accurate anchor calculation
  const float actual_delta_scale = scale / old_scale;

  auto& translation = state_.translation;
  // For persistent magnification, we want the UX to be a little bit different from temporary
  // magnification. In persistent magnification, the user can pan and zoom using a two-finger drag
  // gesture. As the distance between the user’s fingers change, the zoom should change
  // proportionally to the change in distance (so moving the fingers twice as far apart will cause
  // the scale to increase by a factor of 2). So, given the two fingers’ former and current
  // locations, we can compute new_scale = old_scale * (new_distance_between_fingers /
  // old_distance_between_fingers). To achieve panning, our goal is to keep the same point in
  // unscaled space under the centroid of the drag at all times. To do so, we need to consider both
  // the new and previous locations of the centroid (midpoint) between the two fingers. To determine
  // the point in unscaled space that is under the previous centroid of the two fingers, we can
  // simply apply the inverse of the current magnification transform to the previous centroid
  // coordinates. Then, we can compute the new magnification transform by determining what
  // translation will place the unscaled point at the new centroid location (after we’ve applied the
  // new scale factor).
  auto current_centroid =
      ToVec2(state_.gesture_context.CurrentCentroid(false /* use_local_coordinates */));
  translation =
      current_centroid + delta.translation + actual_delta_scale * (translation - current_centroid);

  // Ensure that translation does not fall outside of sensical values.
  ClampTranslation();

  UpdateTransform();
}

void Magnifier2::UpdateTransform() {
  float& transition_rate = state_.transition_rate;
  float& transition_progress = state_.transition_progress;
  bool& update_pending = state_.update_pending;
  bool& update_in_progress = state_.update_in_progress;

  if (!handler_) {
    FX_LOGS(WARNING) << "No magnification handler registered.";

    // If there's no handler, don't bother animating.
    if (transition_rate > 0) {
      transition_progress = 1;
      transition_rate = 0;
    } else if (transition_rate < 0) {
      transition_progress = 0;
      transition_rate = 0;
    }
    return;
  }

  if (update_in_progress) {
    update_pending = true;  // We'll |UpdateTransform| on the next callback instead.
  } else {
    update_in_progress = true;

    if (transition_rate != 0) {
      transition_progress = std::clamp(transition_progress + transition_rate, 0.f, 1.f);

      // If the transition rate is positive, then transition progress of 0
      // indicates that the transition is just beginning, and transition
      // progress of 1 indicatest that the transition is complete.
      // For negative transition rate, transition progress decreases from 1 ->
      // 0.
      //
      // Therefore, this if statement just checks if the transition is not yet
      // complete.
      if ((transition_rate > 0 && transition_progress < 1) ||
          (transition_rate < 0 && transition_progress > 0)) {
        update_pending = true;
      } else {
        transition_rate = 0;
      }
    }

    auto translation_x = transition_progress * state_.translation.x;
    auto translation_y = transition_progress * state_.translation.y;
    auto scale = 1 + transition_progress * (state_.scale - 1);

    handler_->SetClipSpaceTransform(translation_x, translation_y, scale,
                                    handler_scope_.MakeScoped([this] {
                                      state_.update_in_progress = false;
                                      if (state_.update_pending) {
                                        state_.update_pending = false;
                                        UpdateTransform();
                                      }
                                    }));
  }
}

void Magnifier2::TogglePersistentMagnification() {
  if (state_.mode == Mode::UNMAGNIFIED) {
    state_.mode = Mode::PERSISTENT;
    TransitionIntoZoom();
  } else if (state_.mode == Mode::PERSISTENT) {
    state_.mode = Mode::UNMAGNIFIED;
    TransitionOutOfZoom();
  }
}

void Magnifier2::BindGestures(a11y::GestureHandler* gesture_handler) {
  FX_DCHECK(gesture_handler);

  // Add gestures with higher priority earlier than gestures with lower priority.
  bool gesture_bind_status = gesture_handler->BindMFingerNTapAction(
      1 /* number of fingers */, 3 /* number of taps */, [this](GestureContext context) {
        state_.gesture_context = context;
        // One-finger-triple-tap should be a NOOP in temporary magnification
        // mode.
        if (state_.mode == Mode::TEMPORARY) {
          return;
        }
        TogglePersistentMagnification();
      });
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status = gesture_handler->BindMFingerNTapAction(
      3 /* number of fingers */, 2 /* number of taps */, [this](GestureContext context) {
        state_.gesture_context = context;
        // Three-finger-double-tap should be a NOOP in temporary magnification
        // mode.
        if (state_.mode == Mode::TEMPORARY) {
          return;
        }
        TogglePersistentMagnification();
      });
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status = gesture_handler->BindMFingerNTapDragAction(
      [this](GestureContext context) {
        state_.gesture_context = context;
        // Tap-drag gestures should only work to enable temporary magnification
        // from an unmagnified state.
        if (state_.mode != Mode::UNMAGNIFIED) {
          return;
        }
        state_.mode = Mode::TEMPORARY;
        TransitionIntoZoom();
      }, /* on recognize */
      [this](GestureContext context) {
        // We should be in TEMPORARY magnification mode if we hit this callback.
        if (state_.mode != Mode::TEMPORARY) {
          return;
        }
        // TODO(fxb/73255): Verify that we can use the raw centroid here.
        auto delta = GetDelta(context /* current */, state_.gesture_context /* previous */);
        state_.gesture_context = context;
        HandleTemporaryDrag(delta);
      }, /* on update */
      [this](GestureContext context) {
        state_.mode = Mode::UNMAGNIFIED;
        TransitionOutOfZoom();
      }, /* on complete */
      1 /* number of fingers */, 3 /* number of taps */);
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status = gesture_handler->BindMFingerNTapDragAction(
      [this](GestureContext context) {
        state_.gesture_context = context;
        // Tap-drag gestures should only work to enable temporary magnification
        // from an unmagnified state.
        if (state_.mode != Mode::UNMAGNIFIED) {
          return;
        }
        state_.mode = Mode::TEMPORARY;
        TransitionIntoZoom();
      }, /* on recognize */
      [this](GestureContext context) {
        // We should be in TEMPORARY magnification mode if we hit this callback.
        if (state_.mode != Mode::TEMPORARY) {
          return;
        }
        // TODO(fxb/73255): Verify that we can use the raw centroid here.
        auto delta = GetDelta(context /* current */, state_.gesture_context /* previous */);
        state_.gesture_context = context;
        HandleTemporaryDrag(delta);
      }, /* on update */
      [this](GestureContext context) {
        state_.mode = Mode::UNMAGNIFIED;
        TransitionOutOfZoom();
      }, /* on complete */
      3 /* number of fingers */, 2 /* number of taps */);
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status = gesture_handler->BindTwoFingerDragAction(
      [this](GestureContext context) {
        // The magnifier should only respond to two-finger drags when in
        // PERSISTENT magnification mode.
        if (state_.mode != Mode::PERSISTENT) {
          return;
        }
        // Since this is the first event received for the gesture, we don't have
        // any previous context to compare the current one against, so we can't
        // adjust the magnification transform meaningfully yet.
        state_.gesture_context = context;
      }, /* on recognize */
      [this](GestureContext context) {
        // The magnifier should only respond to two-finger drags when in
        // PERSISTENT magnification mode.
        if (state_.mode != Mode::PERSISTENT) {
          return;
        }
        // TODO(fxb/73255): Verify that we can use the raw centroid here.
        auto delta = GetDelta(context /* current */, state_.gesture_context /* previous */);

        HandlePersistentDrag(delta);

        // We need to wait until after we've called HandlePersistentDrag() to
        // update state_.gesture_context, becuase HandlePersistentDrag() needs
        // to use the old gesture_context.
        state_.gesture_context = context;
      }, /* on update */
      [](GestureContext context) { /* NOOP */ } /* on complete */);
  FX_DCHECK(gesture_bind_status);
}

}  // namespace a11y
