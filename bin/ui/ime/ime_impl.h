// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_IME_IME_IMPL_H_
#define APPS_MOZART_SRC_IME_IME_IMPL_H_

#include <memory>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/ui/input/fidl/ime_service.fidl.h"
#include "lib/ui/input/fidl/input_events.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace ime {

class ImeImpl : public mozart::InputMethodEditor {
 public:
  ImeImpl(mozart::KeyboardType keyboard_type,
          mozart::InputMethodAction action,
          mozart::TextInputStatePtr initial_state,
          fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
          fidl::InterfaceRequest<mozart::InputMethodEditor> editor_request);
  ~ImeImpl();

 private:
  // |mozart::InputMethodEditor|
  void SetKeyboardType(mozart::KeyboardType keyboard_type) override;
  void SetState(mozart::TextInputStatePtr state) override;
  void InjectInput(mozart::InputEventPtr event) override;
  void Show() override;
  void Hide() override;

  void OnEditorDied();

  fidl::Binding<mozart::InputMethodEditor> editor_binding_;
  mozart::InputMethodEditorClientPtr client_;
  mozart::KeyboardType keyboard_type_;
  mozart::InputMethodAction action_ = mozart::InputMethodAction::UNSPECIFIED;
  mozart::TextInputStatePtr state_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ImeImpl);
};

}  // namespace ime

#endif  // APPS_MOZART_SRC_IME_IME_IMPL_H_
