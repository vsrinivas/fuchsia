// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include "src/ui/a11y/lib/gesture_manager/interaction.h"
#include "src/ui/a11y/lib/gesture_manager/util.h"

namespace a11y {

GestureManager::GestureManager()
    : binding_(this), gesture_detector_(this), context_(&gesture_handler_) {}

void GestureManager::OnEvent(fuchsia::ui::input::accessibility::PointerEvent pointer_event) {
  auto ptr = ToPointerEvent(pointer_event);
  context_.AddPointerEvent(std::move(pointer_event));
  gesture_detector_.OnPointerEvent(ptr);
  if (ptr.phase == fuchsia::ui::input::PointerEventPhase::ADD) {
    // For now, all pointer events dispatched to accessibility services are consumed.
    // TODO(): Implement consume / reject functionality of pointer events in Gesture Manager.
    binding_.events().OnStreamHandled(ptr.device_id, ptr.pointer_id,
                                      fuchsia::ui::input::accessibility::EventHandling::CONSUMED);
  }
}

std::unique_ptr<input::GestureDetector::Interaction> GestureManager::BeginInteraction(
    const input::Gesture* gesture) {
  return std::make_unique<Interaction>(&context_);
}

}  // namespace a11y
