// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include "fuchsia/input/virtualkeyboard/cpp/fidl.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

namespace root_presenter {

FidlBoundVirtualKeyboardCoordinator::FidlBoundVirtualKeyboardCoordinator(
    sys::ComponentContext* component_context)
    : weak_ptr_factory_(this) {
  FX_DCHECK(component_context);
  component_context->outgoing()
      ->AddPublicService<fuchsia::input::virtualkeyboard::ControllerCreator>(
          [this](
              fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::ControllerCreator> request) {
            creator_bindings_.AddBinding(this, std::move(request), nullptr, [](zx_status_t status) {
              FX_LOGS(INFO) << "controller_creator closed with status=" << status << "( "
                            << zx_status_get_string(status) << ")";
            });
          });
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
  controller_bindings_.AddBinding(std::make_unique<FidlBoundVirtualKeyboardController>(
                                      GetWeakPtr(), std::move(view_ref), text_type),
                                  std::move(controller_request), nullptr, [](zx_status_t status) {
                                    FX_LOGS(INFO) << "controller closed with status=" << status
                                                  << " (" << zx_status_get_string(status) << ")";
                                  });
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
  for (const auto& controller : controller_bindings_.bindings()) {
    FX_DCHECK(controller->impl());
    controller->impl()->OnUserAction(is_visible
                                         ? VirtualKeyboardController::UserAction::SHOW_KEYBOARD
                                         : VirtualKeyboardController::UserAction::HIDE_KEYBOARD);
  }
}

void FidlBoundVirtualKeyboardCoordinator::RequestTypeAndVisibility(
    fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {
  FX_LOGS(INFO) << __PRETTY_FUNCTION__;
  manager_->OnTypeOrVisibilityChange(text_type, is_visible);
}

}  // namespace root_presenter
