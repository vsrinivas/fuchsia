// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_MANAGER_TEXT_INPUT_SERVICE_IMPL_H_
#define APPS_MOZART_SRC_INPUT_MANAGER_TEXT_INPUT_SERVICE_IMPL_H_

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/input/ime_service.fidl.h"
#include "apps/mozart/services/input/input_events.fidl.h"
#include "apps/mozart/services/input/text_input.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace input_manager {

class InputAssociate;

class TextInputServiceImpl : public mozart::TextInputService,
                             mozart::InputMethodEditor,
                             mozart::InputMethodEditorClient {
 public:
  TextInputServiceImpl(InputAssociate* associate,
                       mozart::ViewTokenPtr view_token,
                       fidl::InterfaceRequest<mozart::TextInputService> request,
                       app::ApplicationContext* application_context);
  ~TextInputServiceImpl();

  const mozart::ViewToken* view_token() const { return view_token_.get(); }

  // |mozart::TextInputService|
  void GetInputMethodEditor(
      mozart::KeyboardType keyboard_type,
      mozart::TextInputStatePtr initial_state,
      fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
      fidl::InterfaceRequest<mozart::InputMethodEditor> editor) override;

  // |mozart::InputMethodEditor|
  void SetState(mozart::TextInputStatePtr state) override;
  void SetKeyboardType(mozart::KeyboardType keyboard_type) override;

  // |mozart::InputMethodEditorClient|
  void DidUpdateState(mozart::TextInputStatePtr state,
                      mozart::InputEventPtr event) override;

  void InjectInput(mozart::InputEventPtr event);

 private:
  void OnEditorDied();
  void OnClientDied();
  void ConnectWithImeService(mozart::KeyboardType keyboard_type,
                             mozart::TextInputStatePtr state);
  void Reset();

  // TODO Query to see if it is attached
  bool hardware_keyboard_connected() { return true; }

  InputAssociate* const associate_;
  mozart::ViewTokenPtr view_token_;
  fidl::Binding<mozart::TextInputService> binding_;
  // From the test input
  fidl::Binding<mozart::InputMethodEditor> editor_binding_;
  mozart::InputMethodEditorClientPtr client_;

  // From the IME service
  fidl::Binding<mozart::InputMethodEditorClient> client_binding_;
  mozart::InputMethodEditorPtr editor_;

  mozart::SoftKeyboardContainerPtr container_;
  mozart::ImeServicePtr ime_service_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TextInputServiceImpl);
};

}  // namespace input_manager

#endif  // APPS_MOZART_SRC_INPUT_MANAGER_TEXT_INPUT_SERVICE_IMPL_H_
