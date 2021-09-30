// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

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
  component_context->outgoing()->AddPublicService<fuchsia::input::virtualkeyboard::Manager>(
      [this](fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request) {
        if (manager_binding_) {
          FX_LOGS(WARNING) << "Ignoring interface request; already bound";
        } else {
          BindManager(std::move(request));
        }
      });
}

FidlBoundVirtualKeyboardCoordinator::~FidlBoundVirtualKeyboardCoordinator() {}

// fuchsia.input.virtualkeyboard.ControllerCreator.Create()
void FidlBoundVirtualKeyboardCoordinator::Create(
    fuchsia::ui::views::ViewRef view_ref, fuchsia::input::virtualkeyboard::TextType text_type,
    fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Controller> controller_request) {
  FX_LOGS(INFO) << "ControllerCreator.Create";
  controller_bindings_.AddBinding(std::make_unique<FidlBoundVirtualKeyboardController>(
                                      GetWeakPtr(), std::move(view_ref), text_type),
                                  std::move(controller_request), nullptr, [](zx_status_t status) {
                                    FX_LOGS(INFO) << "controller closed with status=" << status
                                                  << " (" << zx_status_get_string(status) << ")";
                                  });
}

void FidlBoundVirtualKeyboardCoordinator::NotifyVisibilityChange(
    bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason) {
  FX_LOGS(INFO) << __FUNCTION__;
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

void FidlBoundVirtualKeyboardCoordinator::NotifyManagerError(zx_status_t error) {
  if (manager_binding_) {
    manager_binding_->Close(error);
    manager_binding_.reset();
  } else {
    FX_LOGS(ERROR) << "NotifyManagerError called with no manager";
  }
}

void FidlBoundVirtualKeyboardCoordinator::RequestTypeAndVisibility(
    fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {
  FX_LOGS(INFO) << __FUNCTION__;
  if (manager_binding_) {
    manager_binding_->impl()->OnTypeOrVisibilityChange(text_type, is_visible);
  } else {
    pending_manager_config_ = {text_type, is_visible};
  }
}

void FidlBoundVirtualKeyboardCoordinator::NotifyFocusChange(
    fuchsia::ui::views::ViewRef focused_view) {
  FX_NOTIMPLEMENTED();
}

void FidlBoundVirtualKeyboardCoordinator::BindManager(
    fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request) {
  FX_LOGS(INFO) << __FUNCTION__;
  // Initialize the VirtualKeyboardManager, using the zero-value of the TextType enum
  // for the initial TextType.
  auto manager = std::make_unique<VirtualKeyboardManager>(
      GetWeakPtr(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  if (pending_manager_config_) {
    manager->OnTypeOrVisibilityChange(pending_manager_config_->text_type,
                                      pending_manager_config_->is_visible);
    pending_manager_config_.reset();
  }
  manager_binding_.emplace(std::move(manager), std::move(request));
  manager_binding_->set_error_handler(
      [this](zx_status_t status) { HandleManagerBindingError(status); });
}

void FidlBoundVirtualKeyboardCoordinator::HandleManagerBindingError(zx_status_t status) {
  FX_LOGS(WARNING) << "manager closed with status=" << status << " ("
                   << zx_status_get_string(status) << ")";
  manager_binding_.reset();
  // The VirtualKeyboardManager's demise implies that the keyboard is no
  // longer shown. Inform any listening `VirtualKeyboardController`s about
  // this state change.
  NotifyVisibilityChange(false,
                         fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION);
}

}  // namespace root_presenter
