// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include <src/lib/fxl/logging.h>

#include "src/ui/a11y/lib/gesture_manager/interaction.h"

namespace a11y {

namespace {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;
using fuchsia::ui::input::PointerEvent;
using Phase = fuchsia::ui::input::PointerEventPhase;

// Converts an Accessibility Pointer Event to a regular pointer event.
PointerEvent AccessibilityPointerEventToPointerEvent(const AccessibilityPointerEvent& a11y_event) {
  PointerEvent ptr;
  ptr.event_time = a11y_event.event_time();
  ptr.device_id = a11y_event.device_id();
  ptr.pointer_id = a11y_event.pointer_id();
  ptr.type = fuchsia::ui::input::PointerEventType::TOUCH;  // Accessibility Pointer Events are only
                                                           // touch for now.
  ptr.phase = a11y_event.phase();
  // Please note that for detecting a gesture, global coordinates are used. Later,
  // if necessary, local coordinates are sent.
  ptr.x = a11y_event.global_point().x;
  ptr.y = a11y_event.global_point().y;
  return ptr;
}
}  // namespace

GestureManager::GestureManager() : gesture_detector_(this), context_(&gesture_handler_) {}

void GestureManager::OnEvent(AccessibilityPointerEvent pointer_event, OnEventCallback callback) {
  auto ptr = AccessibilityPointerEventToPointerEvent(pointer_event);
  context_.AddPointerEvent(std::move(pointer_event));
  gesture_detector_.OnPointerEvent(ptr);
  if (ptr.phase == Phase::ADD) {
    // For now, all pointer events dispatched to accessibility services are consumed.
    // TODO(): Implement consume / reject functionality of pointer events in Gesture Manager.
    callback(ptr.device_id, ptr.pointer_id,
             fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  }
}

std::unique_ptr<input::GestureDetector::Interaction> GestureManager::BeginInteraction(
    const input::Gesture* gesture) {
  return std::make_unique<Interaction>(&context_);
}

}  // namespace a11y
