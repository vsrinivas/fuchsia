// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/talkback/gesture_detector.h"

namespace talkback {

GestureDetector::GestureDetector(component::StartupContext* startup_context,
                                 TalkbackImpl* talkback)
    : startup_context_(startup_context),
      talkback_(talkback),
      tap_dispatcher_(async_get_default_dispatcher()) {
  touch_dispatcher_.set_error_handler([this]() {
    FXL_LOG(ERROR) << "Cannot connect to a11y touch dispatcher";
  });
  touch_dispatcher_.events().OnInputEvent =
      fit::bind_member(this, &GestureDetector::OnInputEvent);
  touch_dispatcher_.events().OnPresentationChangedEvent =
      fit::bind_member(this, &GestureDetector::OnPresentationChangedEvent);
  startup_context_->ConnectToEnvironmentService(touch_dispatcher_.NewRequest());
}

void GestureDetector::OnInputEvent(fuchsia::ui::input::PointerEvent event) {
  if (event.phase == fuchsia::ui::input::PointerEventPhase::CANCEL) {
    CancelAndIdle();
  }
  switch (state_) {
    case State::kIdle:
      FromIdle(std::move(event));
      break;
    case State::kFirstTouchDown:
      FromFirstTouchDown(std::move(event));
      break;
    case State::kFirstTouchUp:
      FromFirstTouchUp(std::move(event));
      break;
    case State::kSecondTouchDown:
      FromSecondTouchDown(std::move(event));
      break;
    case State::kTwoFingersDown:
      FromTwoFingersDown(std::move(event));
      break;
    default:
      FXL_LOG(FATAL) << "Unreachable state.";
      break;
  }
}

void GestureDetector::OnPresentationChangedEvent(
    fuchsia::ui::viewsv1::ViewTreeToken token) {
  token_ = token;
  CancelAndIdle();
}

void GestureDetector::AfterTapDelay() {
  if (state_ == State::kFirstTouchUp) {
    FXL_VLOG(2) << "AfterTapDelay FirstTouchUp to Idle";
    fuchsia::ui::viewsv1::ViewTreeToken clone_token;
    token_.Clone(&clone_token);
    fuchsia::ui::input::PointerEvent clone_event;
    finger1_pointer_event_.Clone(&clone_event);
    talkback_->SetAccessibilityFocus(std::move(clone_token),
                                     std::move(clone_event));
    state_ = State::kIdle;
  }
}

void GestureDetector::FromIdle(fuchsia::ui::input::PointerEvent event) {
  FXL_DCHECK(state_ == State::kIdle);
  if (event.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
    FXL_VLOG(2) << "Idle to FirstTouchDown";
    finger1_pointer_id_ = event.pointer_id;
    last_pointer_down_or_up_event_ = event.event_time;
    // We set this to 0 rather than event.event_time because if it would
    // render the |kLongPressDelay| obsolete for the first long press event
    // when |kLongPressDelay| < |kMoveCallDelay|.
    last_move_call_ = 0;
    state_ = State::kFirstTouchDown;
    event.Clone(&finger1_pointer_event_);
  }
}

void GestureDetector::FromFirstTouchDown(
    fuchsia::ui::input::PointerEvent event) {
  FXL_DCHECK(state_ == State::kFirstTouchDown);
  if (event.pointer_id != finger1_pointer_id_) {
    // Register that two fingers are down.
    if (event.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
      FXL_VLOG(2) << "FirstTouchDown to TwoFingersDown";
      state_ = State::kTwoFingersDown;
      finger2_pointer_id_ = event.pointer_id;
      finger2_pointer_event_ = event;
      SimulateTouchDown();
    }
    return;
  }
  if (event.phase == fuchsia::ui::input::PointerEventPhase::MOVE) {
    // Move a11y focus if finger has been down > |kLongPressDelay| ns and
    // the last time focus was set > |kMoveCallDelay| ns.
    if (event.event_time - last_pointer_down_or_up_event_ > kLongPressDelay &&
        event.event_time - last_move_call_ > kMoveCallDelay) {
      last_move_call_ = event.event_time;
      fuchsia::ui::viewsv1::ViewTreeToken clone_token_;
      token_.Clone(&clone_token_);
      // TODO(SCN-883): Look into performance costs of setting a11y focus every
      // move input event.
      talkback_->SetAccessibilityFocus(std::move(clone_token_),
                                       std::move(event));
    }
    finger1_pointer_event_ = event;
  } else if (event.phase == fuchsia::ui::input::PointerEventPhase::UP) {
    // End event if finger has been down > |kLongPressDelay| ns.
    if (event.event_time - last_pointer_down_or_up_event_ > kLongPressDelay) {
      FXL_VLOG(2) << "FirstTouchDown to Idle";
      state_ = State::kIdle;
    } else {
      // Continue checking for a double tap. Detect a single tap if the second
      // tap does not start after |kTapDelay| ms.
      FXL_VLOG(2) << "FirstTouchDown to FirstTouchUp";
      state_ = State::kFirstTouchUp;
      last_pointer_down_or_up_event_ = event.event_time;
      async::PostDelayedTask(
          tap_dispatcher_,
          fit::bind_member(this, &GestureDetector::AfterTapDelay), kTapDelay);
    }
  }
}

void GestureDetector::FromFirstTouchUp(fuchsia::ui::input::PointerEvent event) {
  FXL_DCHECK(state_ == State::kFirstTouchUp);
  if (event.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
    FXL_VLOG(2) << "FirstTouchUp to SecondTouchDown";
    finger1_pointer_id_ = event.pointer_id;
    last_pointer_down_or_up_event_ = event.event_time;
    state_ = State::kSecondTouchDown;
  }
}

void GestureDetector::FromSecondTouchDown(
    fuchsia::ui::input::PointerEvent event) {
  FXL_DCHECK(state_ == State::kSecondTouchDown);
  // TODO(SCN-882): Use a11y actions vs. simulated input to support taps,
  // drags, and long presses?
  if (event.phase == fuchsia::ui::input::PointerEventPhase::UP) {
    FXL_VLOG(2) << "SecondTouchDown to Idle";
    talkback_->TapAccessibilityFocusedNode();
    state_ = State::kIdle;
  }
}

void GestureDetector::FromTwoFingersDown(
    fuchsia::ui::input::PointerEvent event) {
  FXL_DCHECK(state_ == State::kTwoFingersDown);
  if (event.pointer_id == finger1_pointer_id_) {
    // Send simulated move events when finger #1 moves.
    if (event.phase == fuchsia::ui::input::PointerEventPhase::MOVE) {
      event.Clone(&finger1_pointer_event_);
      touch_dispatcher_->SendSimulatedPointerEvent(std::move(event));
      finger1_pointer_event_ = event;
    }
    // When finger #1 lifts up, finger #2 is tracked as the new finger #1.
    // The simulated touch event also ends as a finger is lifted.
    else if (event.phase == fuchsia::ui::input::PointerEventPhase::UP) {
      SimulateTouchUp();
      state_ = State::kFirstTouchDown;
      finger1_pointer_id_ = finger2_pointer_id_;
      finger2_pointer_event_.Clone(&finger1_pointer_event_);
    }
  } else if (event.pointer_id == finger2_pointer_id_) {
    // The simulated touch event ends once one finger is lifted.
    if (event.phase == fuchsia::ui::input::PointerEventPhase::UP) {
      SimulateTouchUp();
      state_ = State::kFirstTouchDown;
    } else if (event.phase == fuchsia::ui::input::PointerEventPhase::MOVE) {
      finger2_pointer_event_ = event;
    }
  }
}

void GestureDetector::CancelAndIdle() {
  if (state_ == State::kTwoFingersDown) {
    SimulateCancel();
  }
  state_ = State::kIdle;
}

void GestureDetector::SimulateTouchDown() {
  fuchsia::ui::input::PointerEvent clone_event;
  finger1_pointer_event_.Clone(&clone_event);
  clone_event.phase = fuchsia::ui::input::PointerEventPhase::ADD;
  touch_dispatcher_->SendSimulatedPointerEvent(std::move(clone_event));

  finger1_pointer_event_.Clone(&clone_event);
  clone_event.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
  touch_dispatcher_->SendSimulatedPointerEvent(std::move(clone_event));
}

void GestureDetector::SimulateTouchUp() {
  fuchsia::ui::input::PointerEvent clone_event;
  finger1_pointer_event_.Clone(&clone_event);
  clone_event.phase = fuchsia::ui::input::PointerEventPhase::UP;
  touch_dispatcher_->SendSimulatedPointerEvent(std::move(clone_event));

  finger1_pointer_event_.Clone(&clone_event);
  clone_event.phase = fuchsia::ui::input::PointerEventPhase::REMOVE;
  touch_dispatcher_->SendSimulatedPointerEvent(std::move(clone_event));
}

void GestureDetector::SimulateCancel() {
  fuchsia::ui::input::PointerEvent clone_event;
  finger1_pointer_event_.Clone(&clone_event);
  clone_event.phase = fuchsia::ui::input::PointerEventPhase::CANCEL;
  touch_dispatcher_->SendSimulatedPointerEvent(std::move(clone_event));
}

}  // namespace talkback