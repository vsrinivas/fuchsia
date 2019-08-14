// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_INTERACTION_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_INTERACTION_H_

#include "src/lib/ui/input/gesture_detector.h"
#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"
#include "src/ui/a11y/lib/gesture_manager/interaction_context.h"

namespace a11y {

// This Interaction keeps a state machine which represents a gesture in progress in a touch screen
// device. Once a gesture has been detected, an action bound to this gesture is called via the
// Gesture handler. Please see the base class for more info.
class Interaction : public input::GestureDetector::Interaction {
 public:
  Interaction(InteractionContext* context);
  // |input::GestureDetector::Interaction|
  // When an Interaction ends, if a valid gesture was performed, calls an action
  // bound to this gesture via the Gesture Handler.
  ~Interaction() override;

 private:
  // Please note: the methods bellow may change drastically when time-based taps
  // exist.

  // |input::GestureDetector::Interaction|
  void OnTapBegin(const fuchsia::ui::gfx::vec2& coordinate,
                  input::GestureDetector::TapType tap_type) override;

  // |input::GestureDetector::Interaction|
  void OnTapUpdate(input::GestureDetector::TapType tap_type) override;

  // |input::GestureDetector::Interaction|
  void OnTapCommit() override;

  // |input::GestureDetector::Interaction|
  void OnMultidrag(input::GestureDetector::TapType tap_type,
                   const input::Gesture::Delta& delta) override;

  // State machine states.
  enum InteractionType { kNotStarted, kOneFingerDown, kOneFingerUp, kNotHandled };
  InteractionType state_ = kNotStarted;
  InteractionContext* context_;

  // Optional arguments filled for some detected gestures.
  GestureHandler::GestureArguments args_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_INTERACTION_H_
