// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_IME_APP_H_
#define APPS_MOZART_SRC_IME_APP_H_

#include <memory>
#include <vector>

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/input/ime_service.fidl.h"
#include "apps/mozart/services/input/input_events.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace ime {

class ImeImpl;

class App : public mozart::ImeService {
 public:
  explicit App(const ftl::CommandLine& command_line);
  ~App();

 private:
  // |mozart::ImeService|
  void GetInputMethodEditor(
      mozart::KeyboardType keyboard_type,
      mozart::InputMethodAction action,
      mozart::TextInputStatePtr initial_state,
      fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
      fidl::InterfaceRequest<mozart::InputMethodEditor> editor) override;

  void OnImeDisconnected(ImeImpl* ime);

  std::unique_ptr<app::ApplicationContext> application_context_;
  std::vector<std::unique_ptr<ImeImpl>> ime_;
  fidl::BindingSet<mozart::ImeService> ime_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ime

#endif  // APPS_MOZART_SRC_ROOT_PRESENTER_APP_H_
