// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/tests/mocks/mock_touch_dispatcher.h"

namespace accessibility_test {

void MockTouchDispatcher::Bind(
    fidl::InterfaceRequest<fuchsia::accessibility::TouchDispatcher> request) {
  binding_.Bind(std::move(request));
}

void MockTouchDispatcher::SendPointerEventToClient(
    fuchsia::ui::input::PointerEvent event) {
  binding_.events().OnInputEvent(std::move(event));
}

void MockTouchDispatcher::SendSimulatedPointerEvent(
    fuchsia::ui::input::PointerEvent event) {
  if (callback_ != nullptr) {
    callback_(std::move(event));
  }
}

}  // namespace accessibility_test