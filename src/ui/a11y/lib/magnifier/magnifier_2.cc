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
  state_.mode = Mode::PERSISTENT;
  state_.transition_rate = kTransitionRate;
  UpdateTransform();
}

void Magnifier2::TransitionOutOfZoom() {
  state_.mode = Mode::UNMAGNIFIED;
  state_.transition_rate = -kTransitionRate;
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

void Magnifier2::ToggleMagnification() {
  if (state_.mode == Mode::UNMAGNIFIED) {
    TransitionIntoZoom();
  } else if (state_.mode == Mode::PERSISTENT) {
    TransitionOutOfZoom();
  }
}

void Magnifier2::BindGestures(a11y::GestureHandler* gesture_handler) {
  FX_DCHECK(gesture_handler);

  // Add gestures with higher priority earlier than gestures with lower priority.
  bool gesture_bind_status = gesture_handler->BindMFingerNTapAction(
      1 /* number of fingers */, 3 /* number of taps */, [this](GestureContext context) {
        state_.gesture_context = context;
        ToggleMagnification();
      });
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status = gesture_handler->BindMFingerNTapAction(
      3 /* number of fingers */, 2 /* number of taps */, [this](GestureContext context) {
        state_.gesture_context = context;
        ToggleMagnification();
      });
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status =
      gesture_handler->BindMFingerNTapDragAction([](GestureContext context) {}, /* on recognize */
                                                 [](GestureContext context) {}, /* on update */
                                                 [](GestureContext context) {}, /* on complete */
                                                 1 /* number of fingers */, 3 /* number of taps */);
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status =
      gesture_handler->BindMFingerNTapDragAction([](GestureContext context) {}, /* on recognize */
                                                 [](GestureContext context) {}, /* on update */
                                                 [](GestureContext context) {}, /* on complete */
                                                 3 /* number of fingers */, 2 /* number of taps */);
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status =
      gesture_handler->BindTwoFingerDragAction([](GestureContext context) {}, /* on recognize */
                                               [](GestureContext context) {}, /* on update */
                                               [](GestureContext context) {} /* on complete */);
  FX_DCHECK(gesture_bind_status);
}

}  // namespace a11y
