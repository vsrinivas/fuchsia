// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/input/input_connection_impl.h"

#include "garnet/bin/ui/view_manager/internal/input_owner.h"
#include "garnet/bin/ui/view_manager/internal/view_inspector.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/views/cpp/formatting.h"

namespace view_manager {

InputConnectionImpl::InputConnectionImpl(
    ViewInspector* inspector, InputOwner* owner,
    ::fuchsia::ui::viewsv1token::ViewToken view_token,
    fidl::InterfaceRequest<fuchsia::ui::input::InputConnection> request)
    : inspector_(inspector),
      owner_(owner),
      view_token_(view_token),
      binding_(this, std::move(request)),
      editor_binding_(this),
      client_binding_(this) {
  FXL_DCHECK(inspector_);
  binding_.set_error_handler([this] { owner_->OnInputConnectionDied(this); });
}

InputConnectionImpl::~InputConnectionImpl() {}

void InputConnectionImpl::DeliverEvent(fuchsia::ui::input::InputEvent event,
                                       OnEventDelivered callback) {
  if (!event_listener_) {
    FXL_VLOG(1) << "DeliverEvent: " << view_token_
                << " dropped because there was no listener";
    callback(false);
    return;
  }

  if (event.is_keyboard()) {
    fuchsia::ui::input::InputEvent event_clone;
    event.Clone(&event_clone);
    InjectInput(std::move(event_clone));
  }

  event_listener_->OnEvent(std::move(event),
                           fxl::MakeCopyable(std::move(callback)));
}

void InputConnectionImpl::SetEventListener(
    fidl::InterfaceHandle<fuchsia::ui::input::InputListener> listener) {
  event_listener_ = listener.Bind();
}

void InputConnectionImpl::GetInputMethodEditor(
    fuchsia::ui::input::KeyboardType keyboard_type,
    fuchsia::ui::input::InputMethodAction action,
    fuchsia::ui::input::TextInputState initial_state,
    fidl::InterfaceHandle<fuchsia::ui::input::InputMethodEditorClient> client,
    fidl::InterfaceRequest<fuchsia::ui::input::InputMethodEditor>
        editor_request) {
  FXL_DCHECK(client);
  FXL_DCHECK(editor_request.is_valid());

  FXL_VLOG(1) << "GetInputMethodEditor: view_token=" << view_token_
              << ", keyboard_type=" << fidl::ToUnderlying(keyboard_type)
              << ", initial_state=" << initial_state;

  Reset();

  inspector_->HasFocus(
      view_token_,
      fxl::MakeCopyable([this, editor_request = std::move(editor_request),
                         client = std::move(client), keyboard_type, action,
                         initial_state =
                             std::move(initial_state)](bool focused) mutable {
        FXL_VLOG(1) << "GetInputMethodEditor: " << view_token_ << " "
                    << (focused ? "Focused" : "Not focused");

        if (!focused)
          return;

        editor_binding_.Bind(std::move(editor_request));
        editor_binding_.set_error_handler([this] { OnEditorDied(); });

        client_ = client.Bind();

        if (hardware_keyboard_connected()) {
          ConnectWithImeService(keyboard_type, action,
                                std::move(initial_state));
        } else {
          container_.Unbind();
          inspector_->GetSoftKeyboardContainer(view_token_,
                                               container_.NewRequest());
          container_.set_error_handler([this] {
            FXL_VLOG(1) << "SoftKeyboardContainer died.";
            // TODO if HW Keyboard available, we should fallback to HW IME
            Reset();
          });

          container_->Show(fxl::MakeCopyable(
              [this, keyboard_type, action,
               initial_state = std::move(initial_state)](bool shown) mutable {
                FXL_VLOG(1) << "SoftKeyboardContainer.Show " << shown;
                if (shown) {
                  ConnectWithImeService(keyboard_type, action,
                                        std::move(initial_state));
                }
              }));
        }
      }));
}

void InputConnectionImpl::InjectInput(fuchsia::ui::input::InputEvent event) {
  if (editor_) {
    FXL_VLOG(1) << "InjectInput: view_token=" << view_token_
                << ", event=" << event;
    editor_->InjectInput(std::move(event));
  }
}

void InputConnectionImpl::ConnectWithImeService(
    fuchsia::ui::input::KeyboardType keyboard_type,
    fuchsia::ui::input::InputMethodAction action,
    fuchsia::ui::input::TextInputState state) {
  FXL_VLOG(1) << "ConnectWithImeService: view_token=" << view_token_
              << ", keyboard_type=" << fidl::ToUnderlying(keyboard_type)
              << ", action=" << fidl::ToUnderlying(action)
              << ", initial_state=" << state;
  // Retrieve IME Service from the view tree
  inspector_->GetImeService(view_token_, ime_service_.NewRequest());
  ime_service_.set_error_handler([this] {
    FXL_LOG(ERROR) << "IME Service Died.";
    Reset();
  });

  // GetInputMethodEditor from IME service
  fuchsia::ui::input::InputMethodEditorClientPtr client_ptr;
  client_binding_.Bind(client_ptr.NewRequest());
  client_binding_.set_error_handler([this] { OnClientDied(); });
  ime_service_->GetInputMethodEditor(keyboard_type, action, std::move(state),
                                     std::move(client_ptr),
                                     editor_.NewRequest());
}

void InputConnectionImpl::OnEditorDied() {
  FXL_VLOG(1) << "OnEditorDied: Text 'field' disconnected";
  Reset();
}

void InputConnectionImpl::OnClientDied() {
  FXL_VLOG(1) << "OnClientDied: ImeService disconnected.";
  Reset();
}

void InputConnectionImpl::Reset() {
  if (ime_service_)
    ime_service_.Unbind();

  if (container_) {
    container_->Hide();
    container_.Unbind();
  }

  if (editor_binding_.is_bound())
    editor_binding_.Unbind();
  if (client_)
    client_.Unbind();

  if (editor_)
    editor_.Unbind();
  if (client_binding_.is_bound())
    client_binding_.Unbind();
}

void InputConnectionImpl::SetState(fuchsia::ui::input::TextInputState state) {
  if (editor_) {
    FXL_VLOG(1) << "SetState: view_token=" << view_token_
                << ", state=" << state;
    editor_->SetState(std::move(state));
  } else {
    FXL_VLOG(2) << "Ignoring SetState: view_token=" << view_token_
                << ", state=" << state;
  }
}

void InputConnectionImpl::SetKeyboardType(
    fuchsia::ui::input::KeyboardType keyboard_type) {
  if (editor_) {
    FXL_VLOG(1) << "SetKeyboardType: view_token=" << view_token_
                << ", keyboard_type=" << fidl::ToUnderlying(keyboard_type);
    editor_->SetKeyboardType(keyboard_type);
  } else {
    FXL_VLOG(2) << "Ignoring SetKeyboardType: view_token=" << view_token_
                << ", keyboard_type=" << fidl::ToUnderlying(keyboard_type);
  }
}

void InputConnectionImpl::Show() {}

void InputConnectionImpl::Hide() {}

void InputConnectionImpl::DidUpdateState(
    fuchsia::ui::input::TextInputState state,
    fuchsia::ui::input::InputEventPtr event) {
  if (client_) {
    FXL_VLOG(1) << "DidUpdateState: view_token=" << view_token_
                << ", state=" << state;
    client_->DidUpdateState(std::move(state), std::move(event));
  } else {
    FXL_VLOG(2) << "Ignoring DidUpdateState: view_token=" << view_token_
                << ", state=" << state;
  }
}

void InputConnectionImpl::OnAction(
    fuchsia::ui::input::InputMethodAction action) {
  if (client_) {
    FXL_VLOG(1) << "OnAction: view_token=" << view_token_
                << ", action=" << fidl::ToUnderlying(action);
    client_->OnAction(action);
  } else {
    FXL_VLOG(2) << "Ignoring OnAction: view_token=" << view_token_
                << ", action=" << fidl::ToUnderlying(action);
  }
}

}  // namespace view_manager
