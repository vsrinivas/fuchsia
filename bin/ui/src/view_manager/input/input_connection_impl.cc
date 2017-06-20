// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/input/input_connection_impl.h"

#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/src/view_manager/internal/input_owner.h"
#include "apps/mozart/src/view_manager/internal/view_inspector.h"
#include "lib/ftl/functional/make_copyable.h"

namespace view_manager {

InputConnectionImpl::InputConnectionImpl(
    ViewInspector* inspector,
    InputOwner* owner,
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::InputConnection> request)
    : inspector_(inspector),
      owner_(owner),
      view_token_(std::move(view_token)),
      binding_(this, std::move(request)),
      editor_binding_(this),
      client_binding_(this) {
  FTL_DCHECK(inspector_);
  FTL_DCHECK(view_token_);
  binding_.set_connection_error_handler(
      [this] { owner_->OnInputConnectionDied(this); });
}

InputConnectionImpl::~InputConnectionImpl() {}

void InputConnectionImpl::DeliverEvent(mozart::InputEventPtr event,
                                       OnEventDelivered callback) {
  if (!event_listener_) {
    FTL_VLOG(1) << "DeliverEvent: " << *view_token_
                << " dropped because there was no listener";
    callback(false);
    return;
  }

  if (event->is_keyboard()) {
    InjectInput(event.Clone());
  }

  event_listener_->OnEvent(std::move(event), callback);
}

void InputConnectionImpl::SetEventListener(
    fidl::InterfaceHandle<mozart::InputListener> listener) {
  event_listener_ = mozart::InputListenerPtr::Create(std::move(listener));
}

void InputConnectionImpl::GetInputMethodEditor(
    mozart::KeyboardType keyboard_type,
    mozart::InputMethodAction action,
    mozart::TextInputStatePtr initial_state,
    fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
    fidl::InterfaceRequest<mozart::InputMethodEditor> editor_request) {
  FTL_DCHECK(initial_state);
  FTL_DCHECK(client);
  FTL_DCHECK(editor_request.is_pending());

  FTL_VLOG(1) << "GetInputMethodEditor: view_token=" << *view_token_
              << ", keyboard_type=" << keyboard_type
              << ", initial_state=" << *initial_state;

  Reset();

  inspector_->HasFocus(
      view_token_.Clone(), ftl::MakeCopyable([
        this, editor_request = std::move(editor_request),
        client = std::move(client), keyboard_type, action,
        initial_state = std::move(initial_state)
      ](bool focused) mutable {
        FTL_VLOG(1) << "GetInputMethodEditor: " << *view_token_ << " "
                    << (focused ? "Focused" : "Not focused");

        if (!focused)
          return;

        editor_binding_.Bind(std::move(editor_request));
        editor_binding_.set_connection_error_handler(
            [this] { OnEditorDied(); });

        client_ = mozart::InputMethodEditorClientPtr::Create(std::move(client));

        if (hardware_keyboard_connected()) {
          ConnectWithImeService(keyboard_type, action,
                                std::move(initial_state));
        } else {
          container_.reset();
          inspector_->GetSoftKeyboardContainer(view_token_.Clone(),
                                               container_.NewRequest());
          container_.set_connection_error_handler([this] {
            FTL_VLOG(1) << "SoftKeyboardContainer died.";
            // TODO if HW Keyboard available, we should fallback to HW IME
            Reset();
          });

          container_->Show(ftl::MakeCopyable([
            this, keyboard_type, action,
            initial_state = std::move(initial_state)
          ](bool shown) mutable {
            FTL_VLOG(1) << "SoftKeyboardContainer.Show " << shown;
            if (shown) {
              ConnectWithImeService(keyboard_type, action,
                                    std::move(initial_state));
            }
          }));
        }
      }));
}

void InputConnectionImpl::InjectInput(mozart::InputEventPtr event) {
  if (editor_) {
    FTL_VLOG(1) << "InjectInput: view_token=" << *view_token_
                << ", event=" << *event;
    editor_->InjectInput(std::move(event));
  }
}

void InputConnectionImpl::ConnectWithImeService(
    mozart::KeyboardType keyboard_type,
    mozart::InputMethodAction action,
    mozart::TextInputStatePtr state) {
  FTL_VLOG(1) << "ConnectWithImeService: view_token=" << *view_token_
              << ", keyboard_type=" << keyboard_type << ", action=" << action
              << ", initial_state=" << *state;
  // Retrieve IME Service from the view tree
  inspector_->GetImeService(view_token_.Clone(), ime_service_.NewRequest());
  ime_service_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "IME Service Died.";
    Reset();
  });

  // GetInputMethodEditor from IME service
  mozart::InputMethodEditorClientPtr client_ptr;
  client_binding_.Bind(client_ptr.NewRequest());
  client_binding_.set_connection_error_handler([this] { OnClientDied(); });
  ime_service_->GetInputMethodEditor(keyboard_type, action, std::move(state),
                                     std::move(client_ptr),
                                     editor_.NewRequest());
}

void InputConnectionImpl::OnEditorDied() {
  FTL_VLOG(1) << "OnEditorDied: Text 'field' disconnected";
  Reset();
}

void InputConnectionImpl::OnClientDied() {
  FTL_VLOG(1) << "OnClientDied: ImeService disconnected.";
  Reset();
}

void InputConnectionImpl::Reset() {
  if (ime_service_)
    ime_service_.reset();

  if (container_) {
    container_->Hide();
    container_.reset();
  }

  if (editor_binding_.is_bound())
    editor_binding_.Close();
  if (client_)
    client_.reset();

  if (editor_)
    editor_.reset();
  if (client_binding_.is_bound())
    client_binding_.Close();
}

void InputConnectionImpl::SetState(mozart::TextInputStatePtr state) {
  if (editor_) {
    FTL_VLOG(1) << "SetState: view_token=" << *view_token_
                << ", state=" << *state;
    editor_->SetState(std::move(state));
  } else {
    FTL_VLOG(2) << "Ignoring SetState: view_token=" << *view_token_
                << ", state=" << *state;
  }
}

void InputConnectionImpl::SetKeyboardType(mozart::KeyboardType keyboard_type) {
  if (editor_) {
    FTL_VLOG(1) << "SetKeyboardType: view_token=" << *view_token_
                << ", keyboard_type=" << keyboard_type;
    editor_->SetKeyboardType(keyboard_type);
  } else {
    FTL_VLOG(2) << "Ignoring SetKeyboardType: view_token=" << *view_token_
                << ", keyboard_type=" << keyboard_type;
  }
}

void InputConnectionImpl::Show() {}

void InputConnectionImpl::Hide() {}

void InputConnectionImpl::DidUpdateState(mozart::TextInputStatePtr state,
                                         mozart::InputEventPtr event) {
  if (client_) {
    FTL_VLOG(1) << "DidUpdateState: view_token=" << *view_token_
                << ", state=" << *state;
    client_->DidUpdateState(std::move(state), std::move(event));
  } else {
    FTL_VLOG(2) << "Ignoring DidUpdateState: view_token=" << *view_token_
                << ", state=" << *state;
  }
}

void InputConnectionImpl::OnAction(mozart::InputMethodAction action) {
  if (client_) {
    FTL_VLOG(1) << "OnAction: view_token=" << *view_token_
                << ", action=" << action;
    client_->OnAction(action);
  } else {
    FTL_VLOG(2) << "Ignoring OnAction: view_token=" << *view_token_
                << ", action=" << action;
  }
}

}  // namespace view_manager
