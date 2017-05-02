// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/text_input_service_impl.h"

#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/src/input_manager/input_associate.h"

namespace input_manager {

TextInputServiceImpl::TextInputServiceImpl(
    InputAssociate* associate,
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::TextInputService> request)
    : associate_(associate),
      view_token_(std::move(view_token)),
      binding_(this, std::move(request)),
      editor_binding_(this) {
  FTL_CHECK(associate_);
  FTL_CHECK(view_token_);
  binding_.set_connection_error_handler(
      [this] { associate_->OnTextInputServiceDied(this); });
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

  editor_binding_.Bind(std::move(editor_request));
  editor_binding_.set_connection_error_handler([this] { OnEditorDied(); });

  client_ = mozart::InputMethodEditorClientPtr::Create(std::move(client));
  SetKeyboardType(keyboard_type);
  SetState(std::move(initial_state));
  // TODO Pass client_ to IME
}

void TextInputServiceImpl::OnEditorDied() {
  // TODO close client connection and text input service connection
}

void TextInputServiceImpl::SetState(mozart::TextInputStatePtr state) {
  FTL_DCHECK(!state_ || state_->revision < state->revision);
  state_ = std::move(state);
  // TODO forward to IME
}

void TextInputServiceImpl::SetKeyboardType(mozart::KeyboardType keyboard_type) {
  if (keyboard_type_ != keyboard_type) {
    keyboard_type_ = keyboard_type;
    // TODO forward to IME
  }
}

}  // namespace input_manager
