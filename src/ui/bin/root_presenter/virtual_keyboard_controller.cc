// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace root_presenter {

VirtualKeyboardController::VirtualKeyboardController(
    fuchsia::ui::views::ViewRef view_ref, fuchsia::input::virtualkeyboard::TextType text_type)
    : visible_(false) {}

void VirtualKeyboardController::SetTextType(fuchsia::input::virtualkeyboard::TextType text_type) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
}

void VirtualKeyboardController::RequestShow() {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  visible_ = true;
  MaybeNotifyWatcher();
}

void VirtualKeyboardController::RequestHide() {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  visible_ = false;
  MaybeNotifyWatcher();
}

void VirtualKeyboardController::WatchVisibility(WatchVisibilityCallback callback) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  if (watch_callback_) {
    // Called with a watch already active. Resend the current value, so that
    // the old call doesn't hang forever.
    FX_DCHECK(last_sent_visible_ == visible_);
    watch_callback_(visible_);
  }

  if (last_sent_visible_.has_value()) {
    watch_callback_ = std::move(callback);
    MaybeNotifyWatcher();
  } else {
    callback(visible_);
    last_sent_visible_ = visible_;
  }
}

void VirtualKeyboardController::MaybeNotifyWatcher() {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  if (watch_callback_ && visible_ != last_sent_visible_) {
    watch_callback_(visible_);
    watch_callback_ = {};
    last_sent_visible_ = visible_;
  }
}

}  // namespace root_presenter
