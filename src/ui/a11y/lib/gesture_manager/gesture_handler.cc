// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/a11y/lib/gesture_manager/recognizers/any_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/directional_swipe_recognizers.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/m_finger_n_tap_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/m_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/two_finger_drag_recognizer.h"

namespace a11y {

namespace {

// This recognizer is stateless and trivial, so it makes sense as static.
AnyRecognizer consume_all;

}  // namespace

GestureHandler::GestureHandler(AddRecognizerToArenaCallback add_recognizer_callback)
    : add_recognizer_callback_(std::move(add_recognizer_callback)) {}

void GestureHandler::OnGesture(const GestureType gesture_type, const GestureEvent gesture_event,
                               GestureContext gesture_context) {
  auto it = gesture_handlers_.find(gesture_type);
  if (it == gesture_handlers_.end()) {
    FX_LOGS(INFO) << "GestureHandler::OnGesture: No action found for GestureType:" << gesture_type;
    return;
  }

  switch (gesture_event) {
    case GestureEvent::kRecognize:
      FX_DCHECK(it->second.on_recognize);
      it->second.on_recognize(gesture_context);
      break;
    case GestureEvent::kUpdate:
      FX_DCHECK(it->second.on_update);
      it->second.on_update(gesture_context);
      break;
    case GestureEvent::kComplete:
      FX_DCHECK(it->second.on_complete);
      it->second.on_complete(gesture_context);
      break;
    default:
      break;
  }
}

bool GestureHandler::BindMFingerNTapAction(uint32_t num_fingers, uint32_t num_taps,
                                           OnGestureCallback on_recognize) {
  GestureType gesture_type = kUnknown;

  // This computation is just to make the switch statement cleaner.
  // Since m and n are always <= 3, we can uniquely identify the type of an
  // m-finger-n-tap with the integer (10m + n). E.g. a 3-finger-double-tap would
  // be type 32.
  auto gesture_type_id = 10 * num_fingers + num_taps;

  switch (gesture_type_id) {
    case 11:
      gesture_type = kOneFingerSingleTap;
      break;
    case 12:
      gesture_type = kOneFingerDoubleTap;
      break;
    case 13:
      gesture_type = kOneFingerTripleTap;
      break;
    case 21:
      gesture_type = kTwoFingerSingleTap;
      break;
    case 32:
      gesture_type = kThreeFingerDoubleTap;
      break;
    default:
      break;
  }

  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for GestureType: " << gesture_type;
    return false;
  }

  gesture_handlers_[gesture_type].on_recognize = std::move(on_recognize);

  gesture_recognizers_[gesture_type] = std::make_unique<MFingerNTapRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kRecognize, context);
      },
      num_fingers, num_taps);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

bool GestureHandler::BindOneFingerSingleTapAction(OnGestureCallback callback) {
  return BindOneFingerNTapAction(std::move(callback), 1);
}

bool GestureHandler::BindOneFingerDoubleTapAction(OnGestureCallback callback) {
  return BindOneFingerNTapAction(std::move(callback), 2);
}

bool GestureHandler::BindOneFingerNTapAction(OnGestureCallback callback, int number_of_taps) {
  GestureType gesture_type = kUnknown;
  switch (number_of_taps) {
    case 1:
      gesture_type = kOneFingerSingleTap;
      break;
    case 2:
      gesture_type = kOneFingerDoubleTap;
      break;
    default:
      return false;
  }

  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for GestureType: " << gesture_type;
    return false;
  }
  gesture_handlers_[gesture_type].on_complete = std::move(callback);

  gesture_recognizers_[gesture_type] = std::make_unique<OneFingerNTapRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kComplete, context);
      },
      number_of_taps);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

bool GestureHandler::BindOneFingerDragAction(OnGestureCallback on_recognize,
                                             OnGestureCallback on_update,
                                             OnGestureCallback on_complete) {
  if (gesture_recognizers_.find(kOneFingerDrag) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for one finger drag.";
    return false;
  }
  gesture_handlers_[kOneFingerDrag] = {std::move(on_recognize), std::move(on_update),
                                       std::move(on_complete)};

  gesture_recognizers_[kOneFingerDrag] = std::make_unique<OneFingerDragRecognizer>(
      [this](GestureContext context) {
        OnGesture(kOneFingerDrag, GestureEvent::kRecognize, context);
      }, /* on recognize */
      [this](GestureContext context) {
        OnGesture(kOneFingerDrag, GestureEvent::kUpdate, context);
      }, /* on update */
      [this](GestureContext context) {
        OnGesture(kOneFingerDrag, GestureEvent::kComplete, context);
      } /* on complete */);
  add_recognizer_callback_(gesture_recognizers_[kOneFingerDrag].get());

  return true;
}

bool GestureHandler::BindTwoFingerDragAction(OnGestureCallback on_recognize,
                                             OnGestureCallback on_update,
                                             OnGestureCallback on_complete) {
  if (gesture_recognizers_.find(kTwoFingerDrag) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for two finger drag.";
    return false;
  }
  gesture_handlers_[kTwoFingerDrag] = {std::move(on_recognize), std::move(on_update),
                                       std::move(on_complete)};

  gesture_recognizers_[kTwoFingerDrag] = std::make_unique<TwoFingerDragRecognizer>(
      [this](GestureContext context) {
        OnGesture(kTwoFingerDrag, GestureEvent::kRecognize, context);
      }, /* drag start callback */
      [this](GestureContext context) {
        OnGesture(kTwoFingerDrag, GestureEvent::kUpdate, context);
      }, /* drag update callback */
      [this](GestureContext context) {
        OnGesture(kTwoFingerDrag, GestureEvent::kComplete, context);
      } /* drag completion callback */);
  add_recognizer_callback_(gesture_recognizers_[kTwoFingerDrag].get());

  return true;
}

bool GestureHandler::BindSwipeAction(OnGestureCallback callback, GestureType gesture_type) {
  switch (gesture_type) {
    case kOneFingerUpSwipe:
    case kThreeFingerUpSwipe:
      return BindUpSwipeAction(std::move(callback), gesture_type);

    case kOneFingerDownSwipe:
    case kThreeFingerDownSwipe:
      return BindDownSwipeAction(std::move(callback), gesture_type);

    case kOneFingerLeftSwipe:
    case kThreeFingerLeftSwipe:
      return BindLeftSwipeAction(std::move(callback), gesture_type);
      break;

    case kOneFingerRightSwipe:
    case kThreeFingerRightSwipe:
      return BindRightSwipeAction(std::move(callback), gesture_type);
      break;

    default:
      break;
  }

  return false;
}

bool GestureHandler::BindUpSwipeAction(OnGestureCallback callback, GestureType gesture_type) {
  uint32_t number_of_fingers;
  switch (gesture_type) {
    case kOneFingerUpSwipe:
      number_of_fingers = 1;
      break;
    case kThreeFingerUpSwipe:
      number_of_fingers = 3;
      break;
    default:
      return false;
  }

  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for Up Swipe gesture with " << number_of_fingers
                  << " finger.";
    return false;
  }

  gesture_handlers_[gesture_type].on_complete = std::move(callback);
  gesture_recognizers_[gesture_type] = std::make_unique<UpSwipeGestureRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kComplete, context);
      },
      number_of_fingers);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

bool GestureHandler::BindDownSwipeAction(OnGestureCallback callback, GestureType gesture_type) {
  uint32_t number_of_fingers;
  switch (gesture_type) {
    case kOneFingerDownSwipe:
      number_of_fingers = 1;
      break;
    case kThreeFingerDownSwipe:
      number_of_fingers = 3;
      break;
    default:
      return false;
  }

  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for Down Swipe gesture with " << number_of_fingers
                  << " finger.";
    return false;
  }

  gesture_handlers_[gesture_type].on_complete = std::move(callback);
  gesture_recognizers_[gesture_type] = std::make_unique<DownSwipeGestureRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kComplete, context);
      },
      number_of_fingers);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

bool GestureHandler::BindLeftSwipeAction(OnGestureCallback callback, GestureType gesture_type) {
  uint32_t number_of_fingers;
  switch (gesture_type) {
    case kOneFingerLeftSwipe:
      number_of_fingers = 1;
      break;
    case kThreeFingerLeftSwipe:
      number_of_fingers = 3;
      break;
    default:
      return false;
  }
  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for Left Swipe gesture with " << number_of_fingers
                  << " finger.";
    return false;
  }

  gesture_handlers_[gesture_type].on_complete = std::move(callback);
  gesture_recognizers_[gesture_type] = std::make_unique<LeftSwipeGestureRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kComplete, context);
      },
      number_of_fingers);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

bool GestureHandler::BindRightSwipeAction(OnGestureCallback callback, GestureType gesture_type) {
  uint32_t number_of_fingers;
  switch (gesture_type) {
    case kOneFingerRightSwipe:
      number_of_fingers = 1;
      break;
    case kThreeFingerRightSwipe:
      number_of_fingers = 3;
      break;
    default:
      return false;
  }

  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for Right Swipe gesture with " << number_of_fingers
                  << " finger.";
    return false;
  }

  gesture_handlers_[gesture_type].on_complete = std::move(callback);
  gesture_recognizers_[gesture_type] = std::make_unique<RightSwipeGestureRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kComplete, context);
      },
      number_of_fingers);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

bool GestureHandler::BindTwoFingerSingleTapAction(OnGestureCallback callback) {
  if (gesture_recognizers_.find(kTwoFingerSingleTap) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for GestureType: " << kTwoFingerSingleTap;
    return false;
  }
  gesture_handlers_[kTwoFingerSingleTap].on_complete = std::move(callback);

  gesture_recognizers_[kTwoFingerSingleTap] = std::make_unique<MFingerNTapRecognizer>(
      [this](GestureContext context) {
        OnGesture(kTwoFingerSingleTap, GestureEvent::kComplete, context);
      },
      2,  // number of fingers
      1 /* number of taps */);
  add_recognizer_callback_(gesture_recognizers_[kTwoFingerSingleTap].get());

  return true;
}

bool GestureHandler::BindMFingerNTapDragAction(OnGestureCallback on_recognize,
                                               OnGestureCallback on_update,
                                               OnGestureCallback on_complete, uint32_t num_fingers,
                                               uint32_t num_taps) {
  GestureType gesture_type = kUnknown;

  // This computation is just to make the switch statement cleaner.
  // Since m and n are always <= 3, we can uniquely identify the type of an
  // m-finger-n-tap with the integer (10m + n). E.g. a 3-finger-double-tap would
  // be type 32.
  auto gesture_type_id = 10 * num_fingers + num_taps;

  switch (gesture_type_id) {
    case 13:
      gesture_type = kOneFingerTripleTapDrag;
      break;
    case 32:
      gesture_type = kThreeFingerDoubleTapDrag;
      break;
    default:
      break;
  }

  if (gesture_recognizers_.find(gesture_type) != gesture_recognizers_.end()) {
    FX_LOGS(INFO) << "Action already exists for GestureType: " << gesture_type;
    return false;
  }

  gesture_handlers_[gesture_type].on_recognize = std::move(on_recognize);
  gesture_handlers_[gesture_type].on_update = std::move(on_update);
  gesture_handlers_[gesture_type].on_complete = std::move(on_complete);

  gesture_recognizers_[gesture_type] = std::make_unique<MFingerNTapDragRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kRecognize, context);
      }, /* on recognize */
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kUpdate, context);
      }, /* on update */
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kComplete, context);
      }, /* on complete */
      num_fingers, num_taps);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

void GestureHandler::ConsumeAll() { add_recognizer_callback_(&consume_all); }

}  // namespace a11y
