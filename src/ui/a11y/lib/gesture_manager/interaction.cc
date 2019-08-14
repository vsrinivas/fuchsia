// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/interaction.h"

#include <src/lib/fxl/logging.h>

namespace a11y {

Interaction::Interaction(InteractionContext* context) : context_(context) { FXL_CHECK(context_); }

Interaction::~Interaction() {
  switch (state_) {
    case kOneFingerUp:
      context_->gesture_handler()->OnGesture(GestureHandler::kOneFingerTap, std::move(args_));
      break;
    default:
      break;
  };

  context_->Reset();
}

void Interaction::OnTapBegin(const fuchsia::ui::gfx::vec2& coordinate,
                             input::GestureDetector::TapType tap_type) {
  switch (state_) {
    case kNotStarted: {
      state_ = kOneFingerDown;
      const auto* accessibility_pointer_event = context_->LastAddedEvent();
      FXL_CHECK(accessibility_pointer_event);
      args_.viewref_koid = accessibility_pointer_event->viewref_koid();
      if (accessibility_pointer_event->has_local_point()) {
        args_.coordinates = accessibility_pointer_event->local_point();
      }
      break;
    }
    default:
      state_ = kNotHandled;
      break;
  };
}

void Interaction::OnTapUpdate(input::GestureDetector::TapType tap_type) {
  // TODO(lucasradaelli): implement two/three-finger taps.
  state_ = kNotHandled;
}

void Interaction::OnTapCommit() {
  switch (state_) {
    case kOneFingerDown:
      state_ = kOneFingerUp;
      break;
    default:
      state_ = kNotHandled;
  };
}

void Interaction::OnMultidrag(input::GestureDetector::TapType tap_type,
                              const input::Gesture::Delta& delta) {
  // TODO(lucasradaelli): Implement swipe like gestures.
  state_ = kNotHandled;
}

}  // namespace a11y
