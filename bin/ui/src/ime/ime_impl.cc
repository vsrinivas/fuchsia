// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/ime/ime_impl.h"

#include <hid/usages.h>

#include "apps/mozart/services/input/cpp/formatting.h"
#include "lib/ftl/logging.h"

namespace ime {

ImeImpl::ImeImpl(
    mozart::KeyboardType keyboard_type,
    mozart::TextInputStatePtr initial_state,
    fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
    fidl::InterfaceRequest<mozart::InputMethodEditor> editor_request)
    : editor_binding_(this, std::move(editor_request)),
      keyboard_type_(keyboard_type),
      state_(std::move(initial_state)) {
  FTL_VLOG(1) << "ImeImpl: "
              << ", keyboard_type=" << keyboard_type
              << ", initial_state=" << *state_;

  editor_binding_.set_connection_error_handler([this] { OnEditorDied(); });
  client_ = mozart::InputMethodEditorClientPtr::Create(std::move(client));
}

ImeImpl::~ImeImpl() {}

void ImeImpl::OnEditorDied() {
  FTL_VLOG(1) << "Editor died.";
  // Notify application so we can be cleaned up properly.
}

void ImeImpl::SetKeyboardType(mozart::KeyboardType keyboard_type) {
  FTL_VLOG(1) << "SetKeyboardType: keyboard_type=" << keyboard_type;
  keyboard_type_ = keyboard_type;
}

void ImeImpl::SetState(mozart::TextInputStatePtr state) {
  FTL_VLOG(1) << "SetState: state=" << *state;
  state_ = std::move(state);
}

void ImeImpl::Show() {}
void ImeImpl::Hide() {}

void ImeImpl::InjectInput(mozart::InputEventPtr event) {
  FTL_DCHECK(event->is_keyboard());
  FTL_VLOG(1) << "InjectInput; event=" << *event;
  const mozart::KeyboardEventPtr& keyboard = event->get_keyboard();
  if (keyboard->phase == mozart::KeyboardEvent::Phase::PRESSED ||
      keyboard->phase == mozart::KeyboardEvent::Phase::REPEAT) {
    if (keyboard->code_point) {
      FTL_VLOG(1) << "Appending character (revision = " << state_->revision
                  << ", text='" << state_->text << "')";
      state_->revision++;
      state_->composing->start = 0;
      state_->composing->end = 0;
      std::string text = state_->text.To<std::string>();
      // FIXME (jpoichet) Actually handle UTF8 here
      text += static_cast<char>(keyboard->code_point);
      state_->text = fidl::String(text);
      // FIXME(jpoichet) selection should be taken into account to know where
      // to insert the character and updated accordingly after insert.
      state_->selection->base = text.length();
      state_->selection->extent = text.length();
      state_->selection->affinity = mozart::TextAffinity::DOWNSTREAM;

      FTL_VLOG(1) << "Notifying (revision = " << state_->revision << ", text='"
                  << state_->text << "')";

      client_->DidUpdateState(state_.Clone(), std::move(event));
    } else if (keyboard->hid_usage == HID_USAGE_KEY_BACKSPACE) {
      FTL_VLOG(1) << "Deleting character (revision = " << state_->revision
                  << ", text='" << state_->text << "')";
      state_->revision++;
      state_->composing->start = 0;
      state_->composing->end = 0;
      std::string text = state_->text.To<std::string>();
      text.pop_back();
      state_->text = fidl::String(text);
      state_->selection->base = text.length();
      state_->selection->extent = text.length();
      state_->selection->affinity = mozart::TextAffinity::DOWNSTREAM;

      FTL_VLOG(1) << "Notifying (revision = " << state_->revision << ", text='"
                  << state_->text << "')";

      client_->DidUpdateState(state_.Clone(), std::move(event));
    }
  }
}

}  // namespace ime
