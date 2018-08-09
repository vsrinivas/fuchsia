// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/tests/mocks/mock_gesture_listener.h"

namespace talkback_test {

void MockGestureListener::SetCallback(
    talkback_test::OnGestureDetectedCallback callback) {
  callback_ = std::move(callback);
}

void MockGestureListener::Tap(fuchsia::ui::viewsv1::ViewTreeToken token,
                              fuchsia::ui::input::PointerEvent event) {
  callback_(Gesture::kTap);
}

void MockGestureListener::Move(fuchsia::ui::viewsv1::ViewTreeToken token,
                               fuchsia::ui::input::PointerEvent event) {
  callback_(Gesture::kMove);
}

void MockGestureListener::DoubleTap(fuchsia::ui::viewsv1::ViewTreeToken token,
                                    fuchsia::ui::input::PointerEvent event) {
  callback_(Gesture::kDoubleTap);
}

}  // namespace talkback_test
