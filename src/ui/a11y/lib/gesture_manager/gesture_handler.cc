// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_handler.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/any_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_drag_recognizer.h"
#include "src/ui/a11y/lib/gesture_manager/recognizers/one_finger_n_tap_recognizer.h"

namespace a11y {

namespace {

// This recognizer is stateless and trivial, so it makes sense as static.
AnyRecognizer consume_all;

}  // namespace

GestureHandler::GestureHandler(AddRecognizerToArenaCallback add_recognizer_callback)
    : add_recognizer_callback_(std::move(add_recognizer_callback)) {}

void GestureHandler::OnGesture(const GestureType gesture_type, const GestureEvent gesture_event,
                               GestureArguments args) {
  auto it = gesture_handlers_.find(gesture_type);
  if (it == gesture_handlers_.end()) {
    FX_LOGS(INFO) << "GestureHandler::OnGesture: No action found for GestureType:" << gesture_type;
    return;
  }

  // TODO: Revisit which gestures need coordinates. As currently implemented,
  // all gestures expect them, but they may be unnecessary for some gestures.
  if (args.viewref_koid && args.coordinates) {
    switch (gesture_event) {
      case GestureEvent::kStart:
        FX_DCHECK(it->second.on_start);
        it->second.on_start(*args.viewref_koid, *args.coordinates);
        break;
      case GestureEvent::kUpdate:
        FX_DCHECK(it->second.on_update);
        it->second.on_update(*args.viewref_koid, *args.coordinates);
        break;
      case GestureEvent::kComplete:
        FX_DCHECK(it->second.on_complete);
        it->second.on_complete(*args.viewref_koid, *args.coordinates);
        break;
      default:
        break;
    }
  }
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
    FX_LOGS(ERROR) << "Action already exists for GestureType: " << gesture_type;
    return false;
  }
  gesture_handlers_[gesture_type].on_complete = std::move(callback);

  gesture_recognizers_[gesture_type] = std::make_unique<OneFingerNTapRecognizer>(
      [this, gesture_type](GestureContext context) {
        OnGesture(gesture_type, GestureEvent::kComplete,
                  {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
      },
      number_of_taps);
  add_recognizer_callback_(gesture_recognizers_[gesture_type].get());

  return true;
}

bool GestureHandler::BindOneFingerDragAction(OnGestureCallback on_start,
                                             OnGestureCallback on_update,
                                             OnGestureCallback on_complete) {
  if (gesture_recognizers_.find(kOneFingerDrag) != gesture_recognizers_.end()) {
    FX_LOGS(ERROR) << "Action already exists for one-finger drag gesture.";
    return false;
  }
  gesture_handlers_[kOneFingerDrag] = {std::move(on_start), std::move(on_update),
                                       std::move(on_complete)};

  gesture_recognizers_[kOneFingerDrag] = std::make_unique<OneFingerDragRecognizer>(
      [this](GestureContext context) {
        OnGesture(kOneFingerDrag, GestureEvent::kStart,
                  {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
      }, /* drag start callback */
      [this](GestureContext context) {
        OnGesture(kOneFingerDrag, GestureEvent::kUpdate,
                  {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
      }, /* drag update callback */
      [this](GestureContext context) {
        OnGesture(kOneFingerDrag, GestureEvent::kComplete,
                  {.viewref_koid = context.view_ref_koid, .coordinates = context.local_point});
      } /* drag completion callback */);
  add_recognizer_callback_(gesture_recognizers_[kOneFingerDrag].get());

  return true;
}

void GestureHandler::ConsumeAll() { add_recognizer_callback_(&consume_all); }

}  // namespace a11y
