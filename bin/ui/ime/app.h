// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_IME_APP_H_
#define GARNET_BIN_UI_IME_APP_H_

#include <memory>
#include <vector>

#include <fuchsia/cpp/input.h>
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"

namespace ime {

class ImeImpl;

class App : public input::ImeService {
 public:
  explicit App(const fxl::CommandLine& command_line);
  ~App();

 private:
  // |input::ImeService|
  void GetInputMethodEditor(
      input::KeyboardType keyboard_type,
      input::InputMethodAction action,
      input::TextInputState initial_state,
      fidl::InterfaceHandle<input::InputMethodEditorClient> client,
      fidl::InterfaceRequest<input::InputMethodEditor> editor) override;

  void OnImeDisconnected(ImeImpl* ime);

  std::unique_ptr<component::ApplicationContext> application_context_;
  std::vector<std::unique_ptr<ImeImpl>> ime_;
  fidl::BindingSet<input::ImeService> ime_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ime

#endif  // GARNET_BIN_UI_IME_APP_H_
