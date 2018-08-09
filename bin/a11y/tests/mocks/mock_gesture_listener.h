// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_GESTURE_LISTENER_H_
#define GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_GESTURE_LISTENER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/bin/a11y/talkback/gesture_listener.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace talkback_test {

// Gestures that can be detected
enum class Gesture {
  kTap = 0,
  kMove = 1,
  kDoubleTap = 2,
};

using OnGestureDetectedCallback = fit::function<void(Gesture gesture)>;

class MockGestureListener : public talkback::GestureListener {
 public:
  MockGestureListener() : callback_(nullptr) {}
  ~MockGestureListener() = default;

  void SetCallback(OnGestureDetectedCallback callback);

  void Tap(fuchsia::ui::viewsv1::ViewTreeToken token,
           fuchsia::ui::input::PointerEvent event) override;

  void Move(fuchsia::ui::viewsv1::ViewTreeToken token,
            fuchsia::ui::input::PointerEvent event) override;

  void DoubleTap(fuchsia::ui::viewsv1::ViewTreeToken token,
                 fuchsia::ui::input::PointerEvent event) override;

 private:
  OnGestureDetectedCallback callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockGestureListener);
};

}  // namespace talkback_test

#endif  // GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_GESTURE_LISTENER_H_
