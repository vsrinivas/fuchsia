// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/three_finger_swipe_action.h"

#include <fuchsia/accessibility/gesture/cpp/fidl.h>
#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>

namespace a11y {

ThreeFingerSwipeAction::ThreeFingerSwipeAction(ActionContext* action_context,
                                               ScreenReaderContext* screen_reader_context,
                                               GestureListenerRegistry* gesture_listener_registry,
                                               fuchsia::accessibility::gesture::Type gesture_type)
    : ScreenReaderAction(action_context, screen_reader_context),
      gesture_listener_registry_(gesture_listener_registry),
      gesture_type_(gesture_type) {
  FX_DCHECK(gesture_listener_registry_);
}

ThreeFingerSwipeAction ::~ThreeFingerSwipeAction() = default;

void ThreeFingerSwipeAction::Run(GestureContext gesture_context) {
  if (!gesture_listener_registry_->listener().is_bound()) {
    FX_LOGS(INFO) << "Listener is not registered with Gesture listener registry.";
    return;
  }

  gesture_listener_registry_->listener()->OnGesture(
      gesture_type_, [this](bool handled, fidl::StringPtr utterance) {
        if (!handled) {
          FX_LOGS(INFO) << "Swipe Action is not handled by Gesture Listener.";
          return;
        }

        // Do nothing if utterance is not returned.
        if (utterance == nullptr || utterance->empty()) {
          return;
        }

        // If utterance is present, then send it to TTS.
        fuchsia::accessibility::tts::Utterance tts_utterance;
        tts_utterance.set_message(*utterance);
        auto promise = screen_reader_context_->speaker()
                           ->SpeakMessagePromise(std::move(tts_utterance), {.interrupt = true})
                           // Cancel any promises if this class goes out of scope.
                           .wrap_with(scope_);
        auto* executor = screen_reader_context_->executor();
        executor->schedule_task(std::move(promise));
      });
}

}  // namespace a11y
