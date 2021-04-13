// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_CREATOR_H_
#define SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_CREATOR_H_

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

namespace root_presenter {

// Enables the binding of one or more `fuchsia.input.virtualkeyboard.Controller`s with
// a virtual keyboard. A `VirtualKeyboardControllerCreator` and its `VirtualKeyboardController`s
// are associated with a single virtual keyboard.
class VirtualKeyboardControllerCreator : public fuchsia::input::virtualkeyboard::ControllerCreator {
 public:
  // Constructs an object which can serve the `fuchsia.input.virtualkeyboard.ControllerCreator`
  // FIDL protocol, and publishes the protocol using `component_context`.
  //
  // Callers _should_ construct this object before entering the event loop.
  explicit VirtualKeyboardControllerCreator(sys::ComponentContext* component_context);

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
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_CREATOR_H_
