// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/magnifier_2.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

void Magnifier2::RegisterHandler(
    fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) {
  handler_scope_.Reset();
  state_.update_in_progress = state_.update_pending = false;
  handler_ = handler.Bind();
  // TODO(fxb/69730): Update transform here.
}

void Magnifier2::BindGestures(a11y::GestureHandler* gesture_handler) {
  FX_DCHECK(gesture_handler);

  // Add gestures with higher priority earlier than gestures with lower priority.
  bool gesture_bind_status = gesture_handler->BindMFingerNTapAction(
      1 /* number of fingers */, 3 /* number of taps */, [](GestureContext context) {});
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status = gesture_handler->BindMFingerNTapAction(
      3 /* number of fingers */, 2 /* number of taps */, [](GestureContext gesture_context) {});
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status =
      gesture_handler->BindMFingerNTapDragAction([](GestureContext context) {}, /* on recognize */
                                                 [](GestureContext context) {}, /* on update */
                                                 [](GestureContext context) {}, /* on complete */
                                                 1 /* number of fingers */, 3 /* number of taps */);
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status =
      gesture_handler->BindMFingerNTapDragAction([](GestureContext context) {}, /* on recognize */
                                                 [](GestureContext context) {}, /* on update */
                                                 [](GestureContext context) {}, /* on complete */
                                                 3 /* number of fingers */, 2 /* number of taps */);
  FX_DCHECK(gesture_bind_status);

  gesture_bind_status =
      gesture_handler->BindTwoFingerDragAction([](GestureContext context) {}, /* on recognize */
                                               [](GestureContext context) {}, /* on update */
                                               [](GestureContext context) {} /* on complete */);
  FX_DCHECK(gesture_bind_status);
}

}  // namespace a11y
