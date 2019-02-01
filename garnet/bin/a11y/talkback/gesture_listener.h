// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TALKBACK_GESTURE_LISTENER_H_
#define GARNET_BIN_A11Y_TALKBACK_GESTURE_LISTENER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

namespace talkback {

// Base class for providing hooks for when the gesture detector detects
// a specific gesture.
class GestureListener {
 public:
  virtual ~GestureListener() = default;

  // A quick full touch to the screen.
  virtual void Tap(fuchsia::ui::viewsv1::ViewTreeToken token,
                   fuchsia::ui::input::PointerEvent event) = 0;
  // Finger moving across the screen.
  virtual void Move(fuchsia::ui::viewsv1::ViewTreeToken token,
                    fuchsia::ui::input::PointerEvent event) = 0;

  // Two full touches in quick succession.
  virtual void DoubleTap(fuchsia::ui::viewsv1::ViewTreeToken token,
                         fuchsia::ui::input::PointerEvent event) = 0;
};

}  // namespace talkback

#endif  // GARNET_BIN_A11Y_TALKBACK_GESTURE_LISTENER_H_
