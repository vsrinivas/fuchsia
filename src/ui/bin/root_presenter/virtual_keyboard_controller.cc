// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

#include <lib/syslog/cpp/macros.h>

namespace root_presenter {

VirtualKeyboardController::VirtualKeyboardController(
    fuchsia::ui::views::ViewRef view_ref, fuchsia::input::virtualkeyboard::TextType text_type) {}

void VirtualKeyboardController::SetTextType(fuchsia::input::virtualkeyboard::TextType text_type) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
}

void VirtualKeyboardController::RequestShow() { FX_LOGS(INFO) << __PRETTY_FUNCTION__; }

void VirtualKeyboardController::RequestHide() { FX_LOGS(INFO) << __PRETTY_FUNCTION__; }

void VirtualKeyboardController::WatchVisibility(WatchVisibilityCallback callback) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
}

}  // namespace root_presenter
