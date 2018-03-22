// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_IME_IME_IMPL_H_
#define GARNET_BIN_UI_IME_IME_IMPL_H_

#include <memory>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include <fuchsia/cpp/input.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace ime {

class ImeImpl : public input::InputMethodEditor {
 public:
  ImeImpl(input::KeyboardType keyboard_type,
          input::InputMethodAction action,
          input::TextInputState initial_state,
          fidl::InterfaceHandle<input::InputMethodEditorClient> client,
          fidl::InterfaceRequest<input::InputMethodEditor> editor_request);
  ~ImeImpl();

 private:
  // |input::InputMethodEditor|
  void SetKeyboardType(input::KeyboardType keyboard_type) override;
  void SetState(input::TextInputState state) override;
  void InjectInput(input::InputEvent event) override;
  void Show() override;
  void Hide() override;

  void OnEditorDied();

  fidl::Binding<input::InputMethodEditor> editor_binding_;
  input::InputMethodEditorClientPtr client_;
  input::KeyboardType keyboard_type_;
  input::InputMethodAction action_ = input::InputMethodAction::UNSPECIFIED;
  input::TextInputState state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImeImpl);
};

}  // namespace ime

#endif  // GARNET_BIN_UI_IME_IME_IMPL_H_
