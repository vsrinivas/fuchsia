// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <utility>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

namespace root_presenter {

VirtualKeyboardManager::VirtualKeyboardManager(
    fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator,
    fuchsia::input::virtualkeyboard::TextType initial_text_type)
    : coordinator_(std::move(coordinator)) {
  pending_config_ = KeyboardConfig{.text_type = initial_text_type, .is_visible = false};
}

void VirtualKeyboardManager::WatchTypeAndVisibility(WatchTypeAndVisibilityCallback callback) {
  FX_LOGS(INFO) << __FUNCTION__;
  if (watch_callback_) {
    // The caller has violated the constraints of the protocol. Report the error
    // to the coordinator. The coordinator will close the connection, and destroy
    // this VirtualKeyboardManager.
    if (coordinator_) {
      coordinator_->NotifyManagerError(ZX_ERR_BAD_STATE);
    } else {
      FX_LOGS(WARNING) << "Ignorning redundant WatchTypeAndVisibility() call";
    }
    return;
  }

  watch_callback_ = std::move(callback);
  MaybeNotifyWatcher();
}

void VirtualKeyboardManager::Notify(bool is_visible,
                                    fuchsia::input::virtualkeyboard::VisibilityChangeReason reason,
                                    NotifyCallback callback) {
  FX_LOGS(INFO) << __FUNCTION__ << " is_visible=" << is_visible;
  if (coordinator_) {
    coordinator_->NotifyVisibilityChange(is_visible, reason);
  } else {
    FX_LOGS(WARNING) << "Ignorning Notify() call: no `coordinator_`";
  }
  callback();
}

void VirtualKeyboardManager::OnTypeOrVisibilityChange(
    fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {
  const KeyboardConfig proposed_config = {.text_type = text_type, .is_visible = is_visible};
  if (last_sent_config_ != proposed_config) {
    pending_config_ = proposed_config;
    MaybeNotifyWatcher();
  }
}

void VirtualKeyboardManager::MaybeNotifyWatcher() {
  if (watch_callback_ && pending_config_) {
    watch_callback_(pending_config_->text_type, pending_config_->is_visible);
    last_sent_config_ = pending_config_;
    pending_config_.reset();
    watch_callback_ = {};
  }
}

}  // namespace root_presenter
