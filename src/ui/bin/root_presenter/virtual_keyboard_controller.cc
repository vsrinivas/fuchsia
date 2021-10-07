// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

namespace root_presenter {

FidlBoundVirtualKeyboardController::FidlBoundVirtualKeyboardController(
    fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator, zx_koid_t view_koid,
    fuchsia::input::virtualkeyboard::TextType text_type)
    : coordinator_(std::move(coordinator)),
      view_koid_(view_koid),
      text_type_(text_type),
      want_visible_(false) {}

FidlBoundVirtualKeyboardController::~FidlBoundVirtualKeyboardController() {}

void FidlBoundVirtualKeyboardController::SetTextType(
    fuchsia::input::virtualkeyboard::TextType text_type) {
  FX_LOGS(INFO) << __FUNCTION__;
  text_type_ = text_type;
  NotifyCoordinator();
}

void FidlBoundVirtualKeyboardController::RequestShow() {
  FX_LOGS(INFO) << __FUNCTION__;
  want_visible_ = true;
  NotifyCoordinator();
  MaybeNotifyWatcher();
}

void FidlBoundVirtualKeyboardController::RequestHide() {
  FX_LOGS(INFO) << __FUNCTION__;
  want_visible_ = false;
  NotifyCoordinator();
  MaybeNotifyWatcher();
}

void FidlBoundVirtualKeyboardController::WatchVisibility(WatchVisibilityCallback callback) {
  FX_LOGS(INFO) << __FUNCTION__;
  if (watch_callback_) {
    // Called with a watch already active. Resend the current value, so that
    // the old call doesn't hang forever.
    FX_DCHECK(last_sent_visible_ == want_visible_);
    watch_callback_(want_visible_);
  }

  watch_callback_ = std::move(callback);
  MaybeNotifyWatcher();
}

void FidlBoundVirtualKeyboardController::OnUserAction(UserAction action) {
  switch (action) {
    case UserAction::HIDE_KEYBOARD:
      want_visible_ = false;
      break;
    case UserAction::SHOW_KEYBOARD:
      want_visible_ = true;
      break;
  }
  MaybeNotifyWatcher();
}

void FidlBoundVirtualKeyboardController::MaybeNotifyWatcher() {
  FX_LOGS(INFO) << __FUNCTION__;
  FX_LOGS(DEBUG) << __FUNCTION__ << " want_visible_=" << want_visible_ << " last_sent_visibile_="
                 << (last_sent_visible_.has_value()
                         ? (last_sent_visible_.value() ? "true" : "false")
                         : "(unset)");
  if (watch_callback_ && want_visible_ != last_sent_visible_) {
    watch_callback_(want_visible_);
    watch_callback_ = {};
    last_sent_visible_ = want_visible_;
  }
}

void FidlBoundVirtualKeyboardController::NotifyCoordinator() {
  if (coordinator_) {
    coordinator_->RequestTypeAndVisibility(view_koid_, text_type_, want_visible_);
  } else {
    FX_LOGS(WARNING) << "Ignoring RequestShow()/RequestHide(): no `coordinator_`";
  }
}

}  // namespace root_presenter
