// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_

#include <fuchsia/cpp/input.h>
#include <fuchsia/cpp/views_v1.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"

namespace view_manager {

class ViewInspector;
class InputOwner;

using OnEventDelivered = std::function<void(bool handled)>;

// InputConnection implementation.
// Binds incoming requests to the relevant view token.
class InputConnectionImpl : public input::InputConnection,
                            input::InputMethodEditor,
                            input::InputMethodEditorClient {
 public:
  InputConnectionImpl(ViewInspector* inspector,
                      InputOwner* owner,
                      views_v1_token::ViewTokenPtr view_token,
                      fidl::InterfaceRequest<input::InputConnection> request);
  ~InputConnectionImpl() override;

  const views_v1_token::ViewToken* view_token() const {
    return view_token_.get();
  }

  // Delivers an event to a view.
  void DeliverEvent(input::InputEvent event, OnEventDelivered callback);

  // |input::InputConnection|
  void SetEventListener(
      fidl::InterfaceHandle<input::InputListener> listener) override;
  void GetInputMethodEditor(
      input::KeyboardType keyboard_type,
      input::InputMethodAction action,
      input::TextInputState initial_state,
      fidl::InterfaceHandle<input::InputMethodEditorClient> client,
      fidl::InterfaceRequest<input::InputMethodEditor> editor) override;

  // |input::InputMethodEditor|
  void SetState(input::TextInputState state) override;
  void SetKeyboardType(input::KeyboardType keyboard_type) override;
  void InjectInput(input::InputEvent event) override;
  void Show() override;
  void Hide() override;

  // |input::InputMethodEditorClient|
  void DidUpdateState(input::TextInputState state,
                      input::InputEventPtr event) override;
  void OnAction(input::InputMethodAction action) override;

 private:
  void OnEditorDied();
  void OnClientDied();
  void ConnectWithImeService(input::KeyboardType keyboard_type,
                             input::InputMethodAction action,
                             input::TextInputState state);
  void Reset();

  // TODO(jpoichet) Query to see if it is attached
  bool hardware_keyboard_connected() { return true; }

  ViewInspector* const inspector_;
  InputOwner* const owner_;
  views_v1_token::ViewTokenPtr view_token_;
  input::InputListenerPtr event_listener_;

  fidl::Binding<input::InputConnection> binding_;

  // From the test input
  fidl::Binding<input::InputMethodEditor> editor_binding_;
  input::InputMethodEditorClientPtr client_;

  // From the IME service
  fidl::Binding<input::InputMethodEditorClient> client_binding_;
  input::InputMethodEditorPtr editor_;

  input::SoftKeyboardContainerPtr container_;
  input::ImeServicePtr ime_service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputConnectionImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_
