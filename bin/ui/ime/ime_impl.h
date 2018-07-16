// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_IME_IME_IMPL_H_
#define GARNET_BIN_UI_IME_IME_IMPL_H_

#include <memory>
#include <vector>

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace ime {

class ImeImpl : public fuchsia::ui::input::InputMethodEditor {
 public:
  ImeImpl(
      fuchsia::ui::input::KeyboardType keyboard_type,
      fuchsia::ui::input::InputMethodAction action,
      fuchsia::ui::input::TextInputState initial_state,
      fidl::InterfaceHandle<fuchsia::ui::input::InputMethodEditorClient> client,
      fidl::InterfaceRequest<fuchsia::ui::input::InputMethodEditor>
          editor_request);
  ~ImeImpl();

 private:
  // |fuchsia::ui::input::InputMethodEditor|
  void SetKeyboardType(fuchsia::ui::input::KeyboardType keyboard_type) override;
  void SetState(fuchsia::ui::input::TextInputState state) override;
  void InjectInput(fuchsia::ui::input::InputEvent event) override;
  void Show() override;
  void Hide() override;

  void OnEditorDied();

  fidl::Binding<fuchsia::ui::input::InputMethodEditor> editor_binding_;
  fuchsia::ui::input::InputMethodEditorClientPtr client_;
  fuchsia::ui::input::KeyboardType keyboard_type_;
  fuchsia::ui::input::InputMethodAction action_ =
      fuchsia::ui::input::InputMethodAction::UNSPECIFIED;
  fuchsia::ui::input::TextInputState state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImeImpl);
};

}  // namespace ime

#endif  // GARNET_BIN_UI_IME_IME_IMPL_H_
