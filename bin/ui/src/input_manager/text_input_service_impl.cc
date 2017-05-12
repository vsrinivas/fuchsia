// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/text_input_service_impl.h"

#include "application/lib/app/connect.h"
#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/src/input_manager/input_associate.h"
#include "lib/ftl/functional/make_copyable.h"

namespace input_manager {

TextInputServiceImpl::TextInputServiceImpl(
    InputAssociate* associate,
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::TextInputService> request,
    app::ApplicationContext* application_context)
    : associate_(associate),
      view_token_(std::move(view_token)),
      binding_(this, std::move(request)),
      editor_binding_(this),
      client_binding_(this) {
  FTL_CHECK(associate_);
  FTL_CHECK(view_token_);
  binding_.set_connection_error_handler(
      [this] { associate_->OnTextInputServiceDied(this); });

  app::ConnectToService(application_context->environment_services().get(),
                        ime_service_.NewRequest());
  ime_service_.set_connection_error_handler(
      [] { FTL_LOG(ERROR) << "IME Service Died."; });
}

TextInputServiceImpl::~TextInputServiceImpl() {}

void TextInputServiceImpl::GetInputMethodEditor(
    mozart::KeyboardType keyboard_type,
    mozart::TextInputStatePtr initial_state,
    fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
    fidl::InterfaceRequest<mozart::InputMethodEditor> editor_request) {
  FTL_DCHECK(initial_state);
  FTL_DCHECK(client);
  FTL_DCHECK(editor_request.is_pending());

  FTL_LOG(INFO) << "GetInputMethodEditor: view_token=" << *view_token_
                << ", keyboard_type=" << keyboard_type
                << ", initial_state=" << *initial_state;

  Reset();

  associate_->inspector()->view_inspector()->HasFocus(
      view_token_.Clone(), ftl::MakeCopyable([
        this, editor_request = std::move(editor_request),
        client = std::move(client), keyboard_type,
        initial_state = std::move(initial_state)
      ](bool focused) mutable {
        FTL_LOG(INFO) << "GetInputMethodEditor: " << *view_token_ << " "
                      << (focused ? "Focused" : "Not focused");

        if (!focused)
          return;

        editor_binding_.Bind(std::move(editor_request));
        editor_binding_.set_connection_error_handler(
            [this] { OnEditorDied(); });

        client_ = mozart::InputMethodEditorClientPtr::Create(std::move(client));

        if (hardware_keyboard_connected()) {
          ConnectWithImeService(keyboard_type, std::move(initial_state));
        } else {
          container_.reset();
          associate_->inspector()->view_inspector()->GetSoftKeyboardContainer(
              view_token_.Clone(), container_.NewRequest());
          container_->Show(ftl::MakeCopyable([
            this, keyboard_type, initial_state = std::move(initial_state)
          ](bool shown) mutable {
            if (shown) {
              ConnectWithImeService(keyboard_type, std::move(initial_state));
            }
          }));
        }
      }));
}

void TextInputServiceImpl::InjectInput(mozart::InputEventPtr event) {
  if (editor_) {
    FTL_LOG(INFO) << "InjectInput: view_token=" << *view_token_
                  << ", event=" << *event;
    ime_service_->InjectInput(std::move(event));
  }
}

void TextInputServiceImpl::ConnectWithImeService(
    mozart::KeyboardType keyboard_type,
    mozart::TextInputStatePtr state) {
  FTL_LOG(INFO) << "ConnectWithImeService: view_token=" << *view_token_
                << ", keyboard_type=" << keyboard_type
                << ", initial_state=" << *state;
  FTL_DCHECK(ime_service_.is_bound());
  mozart::InputMethodEditorClientPtr client_ptr;
  client_binding_.Bind(client_ptr.NewRequest());
  client_binding_.set_connection_error_handler([this] { OnClientDied(); });
  ime_service_->GetInputMethodEditor(keyboard_type, std::move(state),
                                     std::move(client_ptr),
                                     editor_.NewRequest());
}

void TextInputServiceImpl::OnEditorDied() {
  FTL_LOG(INFO) << "OnEditorDied: Text 'field' disconnected";
  if (container_) {
    container_->Hide();
    container_.reset();
  }
  Reset();
}

void TextInputServiceImpl::OnClientDied() {
  FTL_LOG(INFO) << "OnClientDied: ImeService disconnected.";
  Reset();
}

void TextInputServiceImpl::Reset() {
  if (editor_binding_.is_bound())
    editor_binding_.Close();
  if (client_)
    client_.reset();

  if (editor_)
    editor_.reset();
  if (client_binding_.is_bound())
    client_binding_.Close();
}

void TextInputServiceImpl::SetState(mozart::TextInputStatePtr state) {
  if (editor_) {
    FTL_LOG(INFO) << "SetState: view_token=" << *view_token_
                  << ", state=" << *state;
    editor_->SetState(std::move(state));
  } else {
    FTL_VLOG(2) << "Ignoring SetState: view_token=" << *view_token_
                << ", state=" << *state;
  }
}

void TextInputServiceImpl::SetKeyboardType(mozart::KeyboardType keyboard_type) {
  if (editor_) {
    FTL_LOG(INFO) << "SetKeyboardType: view_token=" << *view_token_
                  << ", keyboard_type=" << keyboard_type;
    editor_->SetKeyboardType(keyboard_type);
  } else {
    FTL_VLOG(2) << "Ignoring SetKeyboardType: view_token=" << *view_token_
                << ", keyboard_type=" << keyboard_type;
  }
}

void TextInputServiceImpl::DidUpdateState(mozart::TextInputStatePtr state,
                                          mozart::InputEventPtr event) {
  if (client_) {
    FTL_LOG(INFO) << "DidUpdateState: view_token=" << *view_token_
                  << ", state=" << *state;
    client_->DidUpdateState(std::move(state), std::move(event));
  } else {
    FTL_VLOG(2) << "Ignoring DidUpdateState: view_token=" << *view_token_
                << ", state=" << *state;
  }
}

}  // namespace input_manager
