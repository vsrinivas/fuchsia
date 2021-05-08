// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <utility>

#include "lib/fidl/cpp/interface_request.h"
#include "lib/sys/cpp/component_context.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace root_presenter {

VirtualKeyboardManager::VirtualKeyboardManager(fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator,
                                               sys::ComponentContext* component_context)
    : coordinator_(std::move(coordinator)), manager_binding_(this) {
  FX_DCHECK(component_context);
  component_context->outgoing()->AddPublicService<fuchsia::input::virtualkeyboard::Manager>(
      [this](fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request) {
        MaybeBind(std::move(request));
      });
}

void VirtualKeyboardManager::WatchTypeAndVisibility(WatchTypeAndVisibilityCallback callback) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
}

void VirtualKeyboardManager::Notify(bool is_visible,
                                    fuchsia::input::virtualkeyboard::VisibilityChangeReason reason,
                                    NotifyCallback callback) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
}

void VirtualKeyboardManager::MaybeBind(
    fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request) {
  if (manager_binding_.is_bound()) {
    FX_LOGS(INFO) << "Ignoring interface request; already bound";
  } else {
    manager_binding_.Bind(std::move(request));
  }
}

}  // namespace root_presenter
