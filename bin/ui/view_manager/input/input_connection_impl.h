// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_

#include "lib/ui/input/fidl/ime_service.fidl.h"
#include "lib/ui/input/fidl/input_connection.fidl.h"
#include "lib/ui/input/fidl/text_input.fidl.h"
#include "lib/ui/views/fidl/views.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"

namespace view_manager {

class ViewInspector;
class InputOwner;

using OnEventDelivered = std::function<void(bool handled)>;

// InputConnection implementation.
// Binds incoming requests to the relevant view token.
class InputConnectionImpl : public mozart::InputConnection,
                            mozart::InputMethodEditor,
                            mozart::InputMethodEditorClient {
 public:
  InputConnectionImpl(ViewInspector* inspector,
                      InputOwner* owner,
                      mozart::ViewTokenPtr view_token,
                      fidl::InterfaceRequest<mozart::InputConnection> request);
  ~InputConnectionImpl() override;

  const mozart::ViewToken* view_token() const { return view_token_.get(); }

  // Delivers an event to a view.
  void DeliverEvent(mozart::InputEventPtr event, OnEventDelivered callback);

  // |mozart::InputConnection|
  void SetEventListener(
      fidl::InterfaceHandle<mozart::InputListener> listener) override;
  void GetInputMethodEditor(
      mozart::KeyboardType keyboard_type,
      mozart::InputMethodAction action,
      mozart::TextInputStatePtr initial_state,
      fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
      fidl::InterfaceRequest<mozart::InputMethodEditor> editor) override;

  // |mozart::InputMethodEditor|
  void SetState(mozart::TextInputStatePtr state) override;
  void SetKeyboardType(mozart::KeyboardType keyboard_type) override;
  void InjectInput(mozart::InputEventPtr event) override;
  void Show() override;
  void Hide() override;

  // |mozart::InputMethodEditorClient|
  void DidUpdateState(mozart::TextInputStatePtr state,
                      mozart::InputEventPtr event) override;
  void OnAction(mozart::InputMethodAction action) override;

 private:
  void OnEditorDied();
  void OnClientDied();
  void ConnectWithImeService(mozart::KeyboardType keyboard_type,
                             mozart::InputMethodAction action,
                             mozart::TextInputStatePtr state);
  void Reset();

  // TODO(jpoichet) Query to see if it is attached
  bool hardware_keyboard_connected() { return true; }

  ViewInspector* const inspector_;
  InputOwner* const owner_;
  mozart::ViewTokenPtr view_token_;
  mozart::InputListenerPtr event_listener_;

  fidl::Binding<mozart::InputConnection> binding_;

  // From the test input
  fidl::Binding<mozart::InputMethodEditor> editor_binding_;
  mozart::InputMethodEditorClientPtr client_;

  // From the IME service
  fidl::Binding<mozart::InputMethodEditorClient> client_binding_;
  mozart::InputMethodEditorPtr editor_;

  mozart::SoftKeyboardContainerPtr container_;
  mozart::ImeServicePtr ime_service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputConnectionImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_
