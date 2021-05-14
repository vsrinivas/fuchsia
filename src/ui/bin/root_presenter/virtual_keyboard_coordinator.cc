// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "fuchsia/input/virtualkeyboard/cpp/fidl.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

namespace root_presenter {

FidlBoundVirtualKeyboardCoordinator::FidlBoundVirtualKeyboardCoordinator(
    sys::ComponentContext* component_context)
    : weak_ptr_factory_(this) {
  FX_DCHECK(component_context);
  component_context->outgoing()->AddPublicService(creator_bindings_.GetHandler(this));
  // Initialize the VirtualKeyboardManager, using the zero-value of the TextType enum
  // for the initial TextType.
  manager_.emplace(GetWeakPtr(), component_context,
                   fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
}

FidlBoundVirtualKeyboardCoordinator::~FidlBoundVirtualKeyboardCoordinator() {}

// fuchsia.input.virtualkeyboard.ControllerCreator.Create()
void FidlBoundVirtualKeyboardCoordinator::Create(
    fuchsia::ui::views::ViewRef view_ref, fuchsia::input::virtualkeyboard::TextType text_type,
    fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Controller> controller_request) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  controller_binding_ =
      std::make_unique<ControllerBinding>(std::make_unique<FidlBoundVirtualKeyboardController>(
                                              GetWeakPtr(), std::move(view_ref), text_type),
                                          std::move(controller_request));
}

void FidlBoundVirtualKeyboardCoordinator::NotifyVisibilityChange(
    bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  if (reason.IsUnknown()) {
    FX_LOGS(WARNING) << "Ignorning visibility change with reason = " << reason;
    return;
  }

  if (reason == fuchsia::input::virtualkeyboard::VisibilityChangeReason::PROGRAMMATIC) {
    // `Controller` remembers its own changes, so no need to echo them back.
    return;
  }

  FX_DCHECK(reason == fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION);
  if (!controller_binding_) {
    // Any user action before a `Controller` is bound does not affect whether
    // or not the `Controller` wants to show the keyboard. So we don't need
    // to buffer the notification.
    return;
  }

  FX_DCHECK(controller_binding_->impl());
  controller_binding_->impl()->OnUserAction(
      is_visible ? VirtualKeyboardController::UserAction::SHOW_KEYBOARD
                 : VirtualKeyboardController::UserAction::HIDE_KEYBOARD);
}

void FidlBoundVirtualKeyboardCoordinator::RequestTypeAndVisibility(
    fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  manager_->OnTypeOrVisibilityChange(text_type, is_visible);
}

}  // namespace root_presenter
