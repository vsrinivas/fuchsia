// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <utility>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

namespace root_presenter {

VirtualKeyboardManager::VirtualKeyboardManager(
    fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator, sys::ComponentContext* component_context,
    fuchsia::input::virtualkeyboard::TextType initial_text_type)
    : coordinator_(std::move(coordinator)), manager_binding_(this) {
  FX_DCHECK(component_context);
  pending_config_ = KeyboardConfig{.text_type = initial_text_type, .is_visible = false};
  component_context->outgoing()->AddPublicService<fuchsia::input::virtualkeyboard::Manager>(
      [this](fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request) {
        MaybeBind(std::move(request));
      });
}

void VirtualKeyboardManager::WatchTypeAndVisibility(WatchTypeAndVisibilityCallback callback) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  if (watch_callback_) {
    // The caller has violated the constraints of the protocol. Close the connection
    // to signal the error, and reset the callback, to ensure that other method calls
    // on |this| don't write to the closed connection.
    manager_binding_.Close(ZX_ERR_BAD_STATE);
    watch_callback_ = {};
    return;
  }

  watch_callback_ = std::move(callback);
  MaybeNotifyWatcher();
}

void VirtualKeyboardManager::Notify(bool is_visible,
                                    fuchsia::input::virtualkeyboard::VisibilityChangeReason reason,
                                    NotifyCallback callback) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
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

void VirtualKeyboardManager::MaybeBind(
    fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request) {
  if (manager_binding_.is_bound()) {
    FX_LOGS(INFO) << "Ignoring interface request; already bound";
  } else {
    manager_binding_.Bind(std::move(request));
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
