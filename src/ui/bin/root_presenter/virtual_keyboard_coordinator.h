// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_COORDINATOR_H_
#define SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_COORDINATOR_H_

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

namespace root_presenter {

// Coordinates all activities for a single virtual keyboard.
//
// This includes:
// * Publishing the `fuchsia.input.virtualkeyboard.ControllerCreator` FIDL
//   protocol, and binding `VirtualKeyboardController`s to the virtual keyboard.
// * Publishing the `fuchsia.input.virtualkeyboard.Manager` FIDL protocol, and
//   binding a `VirtualKeyboardManager` to the virtual keyboard.
class VirtualKeyboardCoordinator : public fuchsia::input::virtualkeyboard::ControllerCreator {
 public:
  // Constructs a VirtualKeyboardCoordinator, and publishes the relevant FIDLs
  // using `component_context`.
  //
  // Callers _should_ construct this object before entering the event loop.
  explicit VirtualKeyboardCoordinator(sys::ComponentContext* component_context);

  fxl::WeakPtr<VirtualKeyboardCoordinator> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  using ControllerBinding =
      fidl::Binding<fuchsia::input::virtualkeyboard::Controller,
                    std::unique_ptr<fuchsia::input::virtualkeyboard::Controller>>;

  // |fuchsia.input.virtualkeyboard.ControllerCreator|
  void Create(fuchsia::ui::views::ViewRef view_ref,
              fuchsia::input::virtualkeyboard::TextType text_type,
              fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Controller>
                  controller_request) override;

  fidl::BindingSet<fuchsia::input::virtualkeyboard::ControllerCreator> creator_bindings_;
  std::unique_ptr<ControllerBinding> controller_binding_;

  std::optional<VirtualKeyboardManager> manager_;

  // Must be last, to invalidate weak pointers held by other fields before their
  // destructors are called.
  fxl::WeakPtrFactory<VirtualKeyboardCoordinator> weak_ptr_factory_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_COORDINATOR_H_
