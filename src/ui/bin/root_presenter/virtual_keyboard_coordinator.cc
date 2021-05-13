// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

namespace root_presenter {

FidlBoundVirtualKeyboardCoordinator::FidlBoundVirtualKeyboardCoordinator(
    sys::ComponentContext* component_context)
    : weak_ptr_factory_(this) {
  FX_DCHECK(component_context);
  component_context->outgoing()->AddPublicService(creator_bindings_.GetHandler(this));
  manager_.emplace(GetWeakPtr(), component_context);
}

FidlBoundVirtualKeyboardCoordinator::~FidlBoundVirtualKeyboardCoordinator() {}

// fuchsia.input.virtualkeyboard.ControllerCreator.Create()
void FidlBoundVirtualKeyboardCoordinator::Create(
    fuchsia::ui::views::ViewRef view_ref, fuchsia::input::virtualkeyboard::TextType text_type,
    fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Controller> controller_request) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  controller_binding_ = std::make_unique<ControllerBinding>(
      std::make_unique<VirtualKeyboardController>(GetWeakPtr(), std::move(view_ref), text_type),
      std::move(controller_request));
}

void FidlBoundVirtualKeyboardCoordinator::NotifyVisibilityChange(
    bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
}

void FidlBoundVirtualKeyboardCoordinator::RequestTypeAndVisibility(
    fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
}

}  // namespace root_presenter
