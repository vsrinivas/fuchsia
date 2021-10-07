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
  component_context->outgoing()->AddPublicService<fuchsia::input::virtualkeyboard::Manager>(
      [this](fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request) {
        if (manager_binding_) {
          FX_LOGS(WARNING) << "Ignoring Manager interface request; already bound";
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
  zx_info_handle_basic_t view_ref_info{};
  zx_koid_t view_koid = ZX_KOID_INVALID;
  if (auto status = view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &view_ref_info,
                                                sizeof(view_ref_info), nullptr, nullptr);
      status != ZX_OK) {
    FX_LOGS(ERROR) << __FUNCTION__ << ": failed to get koid for view ref ("
                   << zx_status_get_string(status) << ")";
    return;
  }
  view_koid = view_ref_info.koid;

  auto controller =
      std::make_unique<FidlBoundVirtualKeyboardController>(GetWeakPtr(), view_koid, text_type);
  controller_bindings_.AddBinding(std::move(controller), std::move(controller_request), nullptr,
                                  [view_koid](zx_status_t status) {
                                    FX_LOGS(INFO) << "controller for view_koid=" << view_koid
                                                  << " closed with status=" << status << " ("
                                                  << zx_status_get_string(status) << ")";
                                  });
}

void FidlBoundVirtualKeyboardCoordinator::NotifyVisibilityChange(
    bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason) {
  FX_LOGS(INFO) << __FUNCTION__;
  if (reason.IsUnknown()) {
    FX_LOGS(WARNING) << __FUNCTION__ << ": ignorning visibility change with reason = " << reason;
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
    zx_koid_t requestor_view_koid, fuchsia::input::virtualkeyboard::TextType text_type,
    bool is_visible) {
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
  FX_LOGS(WARNING) << __FUNCTION__ << ": status=" << status << " (" << zx_status_get_string(status)
                   << ")";
  manager_binding_.reset();
  // The VirtualKeyboardManager's demise implies that the keyboard is no
  // longer shown. Inform any listening `VirtualKeyboardController`s about
  // this state change.
  NotifyVisibilityChange(false,
                         fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION);
}

}  // namespace root_presenter
