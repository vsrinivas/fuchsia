// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_listener.h"

namespace accessibility_test {

MockGestureListener::MockGestureListener() : binding_(this) {
  binding_.set_error_handler([this](zx_status_t status) { is_registered_ = false; });
}

fidl::InterfaceHandle<fuchsia::accessibility::gesture::Listener> MockGestureListener::NewBinding() {
  is_registered_ = true;
  return binding_.NewBinding();
}

bool MockGestureListener::is_registered() const { return is_registered_; }

void MockGestureListener::OnGesture(
    fuchsia::accessibility::gesture::Type gesture_type,
    fuchsia::accessibility::gesture::Listener::OnGestureCallback callback) {
  gesture_type_ = gesture_type;
  callback(on_gesture_callback_status_, utterance_);
}

void MockGestureListener::SetUtterance(std::string utterance) {
  if (utterance.empty()) {
    utterance_ = nullptr;
    return;
  }

  utterance_ = fidl::StringPtr(std::move(utterance));
}

void MockGestureListener::SetOnGestureCallbackStatus(bool status) {
  on_gesture_callback_status_ = status;
}

void MockGestureListener::SetGestureType(fuchsia::accessibility::gesture::Type gesture_type) {
  gesture_type_ = gesture_type;
}

fuchsia::accessibility::gesture::Type MockGestureListener::gesture_type() const {
  return gesture_type_;
}

}  // namespace accessibility_test
