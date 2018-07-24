// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"

namespace view_manager {

class ViewInspector;
class InputOwner;

using OnEventDelivered = fit::function<void(bool handled)>;

// InputConnection implementation.
// Binds incoming requests to the relevant view token.
class InputConnectionImpl : public fuchsia::ui::input::InputConnection,
                            fuchsia::ui::input::InputMethodEditor,
                            fuchsia::ui::input::InputMethodEditorClient {
 public:
  InputConnectionImpl(
      ViewInspector* inspector, InputOwner* owner,
      ::fuchsia::ui::viewsv1token::ViewToken view_token,
      fidl::InterfaceRequest<fuchsia::ui::input::InputConnection> request);
  ~InputConnectionImpl() override;

  ::fuchsia::ui::viewsv1token::ViewToken view_token() const {
    return view_token_;
  }

  // Delivers an event to a view.
  void DeliverEvent(fuchsia::ui::input::InputEvent event,
                    OnEventDelivered callback);

  // |fuchsia::ui::input::InputConnection|
  void SetEventListener(fidl::InterfaceHandle<fuchsia::ui::input::InputListener>
                            listener) override;
  void GetInputMethodEditor(
      fuchsia::ui::input::KeyboardType keyboard_type,
      fuchsia::ui::input::InputMethodAction action,
      fuchsia::ui::input::TextInputState initial_state,
      fidl::InterfaceHandle<fuchsia::ui::input::InputMethodEditorClient> client,
      fidl::InterfaceRequest<fuchsia::ui::input::InputMethodEditor> editor)
      override;

  // |fuchsia::ui::input::InputMethodEditor|
  void SetState(fuchsia::ui::input::TextInputState state) override;
  void SetKeyboardType(fuchsia::ui::input::KeyboardType keyboard_type) override;
  void InjectInput(fuchsia::ui::input::InputEvent event) override;
  void Show() override;
  void Hide() override;

  // |fuchsia::ui::input::InputMethodEditorClient|
  void DidUpdateState(fuchsia::ui::input::TextInputState state,
                      fuchsia::ui::input::InputEventPtr event) override;
  void OnAction(fuchsia::ui::input::InputMethodAction action) override;

 private:
  void OnEditorDied();
  void OnClientDied();
  void ConnectWithImeService(fuchsia::ui::input::KeyboardType keyboard_type,
                             fuchsia::ui::input::InputMethodAction action,
                             fuchsia::ui::input::TextInputState state);
  void Reset();

  // TODO(jpoichet) Query to see if it is attached
  bool hardware_keyboard_connected() { return true; }

  ViewInspector* const inspector_;
  InputOwner* const owner_;
  ::fuchsia::ui::viewsv1token::ViewToken view_token_;
  fuchsia::ui::input::InputListenerPtr event_listener_;

  fidl::Binding<fuchsia::ui::input::InputConnection> binding_;

  // From the test input
  fidl::Binding<fuchsia::ui::input::InputMethodEditor> editor_binding_;
  fuchsia::ui::input::InputMethodEditorClientPtr client_;

  // From the IME service
  fidl::Binding<fuchsia::ui::input::InputMethodEditorClient> client_binding_;
  fuchsia::ui::input::InputMethodEditorPtr editor_;

  fuchsia::ui::input::SoftKeyboardContainerPtr container_;
  fuchsia::ui::input::ImeServicePtr ime_service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputConnectionImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_CONNECTION_IMPL_H_
