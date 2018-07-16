// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_IME_APP_H_
#define GARNET_BIN_UI_IME_APP_H_

#include <memory>
#include <vector>

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace ime {

class ImeImpl;

class App : public fuchsia::ui::input::ImeService {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

 private:
  // |fuchsia::ui::input::ImeService|
  void GetInputMethodEditor(
      fuchsia::ui::input::KeyboardType keyboard_type,
      fuchsia::ui::input::InputMethodAction action,
      fuchsia::ui::input::TextInputState initial_state,
      fidl::InterfaceHandle<fuchsia::ui::input::InputMethodEditorClient> client,
      fidl::InterfaceRequest<fuchsia::ui::input::InputMethodEditor> editor)
      override;

  void OnImeDisconnected(ImeImpl* ime);

  std::unique_ptr<component::StartupContext> startup_context_;
  std::vector<std::unique_ptr<ImeImpl>> ime_;
  fidl::BindingSet<fuchsia::ui::input::ImeService> ime_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ime

#endif  // GARNET_BIN_UI_IME_APP_H_
