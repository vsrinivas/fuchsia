// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/ime/ime_impl.h"

#include <hid/usages.h>

#include "lib/ui/input/cpp/formatting.h"
#include "lib/ftl/logging.h"

namespace ime {

ImeImpl::ImeImpl(
    mozart::KeyboardType keyboard_type,
    mozart::InputMethodAction action,
    mozart::TextInputStatePtr initial_state,
    fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
    fidl::InterfaceRequest<mozart::InputMethodEditor> editor_request)
    : editor_binding_(this, std::move(editor_request)),
      keyboard_type_(keyboard_type),
      action_(action),
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
      FTL_VLOG(1) << "Appending character (state = " << *state_ << "')";
      state_->revision++;
      std::string text = state_->text.To<std::string>();
      // FIXME (jpoichet) Actually handle UTF8 here
      std::string replacement = "";
      replacement += static_cast<char>(keyboard->code_point);
      int64_t base = state_->selection->base;
      base = base == -1 ? 0 : base;
      int64_t extent = state_->selection->extent;
      extent = extent == -1 ? 0 : extent;
      text.replace(base, extent - base, replacement);
      state_->text = fidl::String(text);
      state_->selection->base = base + replacement.length();
      state_->selection->extent = state_->selection->base;

      FTL_VLOG(1) << "Notifying (state = " << *state_ << "')";
      client_->DidUpdateState(state_.Clone(), std::move(event));
    } else {
      switch (keyboard->hid_usage) {
        case HID_USAGE_KEY_BACKSPACE: {
          FTL_VLOG(1) << "Deleting character (state = " << *state_ << "')";
          std::string text = state_->text.To<std::string>();
          int64_t base = state_->selection->base;
          int64_t extent = state_->selection->extent;
          if (base == -1 && extent == -1) {
            base = state_->text.size() - 1;
            extent = base + 1;
          } else if (base == extent) {
            if (base != 0) {
              base--;
            }
          }
          state_->revision++;
          text.erase(base, extent - base);
          state_->text = fidl::String(text);
          state_->selection->base = base;
          state_->selection->extent = state_->selection->base;

          FTL_VLOG(1) << "Notifying (state = " << *state_ << "')";
          client_->DidUpdateState(state_.Clone(), std::move(event));
        } break;
        case HID_USAGE_KEY_LEFT: {
          FTL_VLOG(1) << "Moving left (state = " << *state_ << "')";
          state_->revision++;
          // TODO(jpoichet) actually pay attention to affinity
          state_->selection->base = state_->selection->base > 0
                                        ? state_->selection->base - 1
                                        : state_->selection->base;
          if (keyboard->modifiers & mozart::kModifierShift) {
          } else {
            state_->selection->extent = state_->selection->base;
          }

          FTL_VLOG(1) << "Notifying (state = " << *state_ << "')";
          client_->DidUpdateState(state_.Clone(), std::move(event));
        } break;
        case HID_USAGE_KEY_RIGHT: {
          FTL_VLOG(1) << "Moving right (state = " << *state_ << "')";
          state_->revision++;
          // TODO(jpoichet) actually pay attention to affinity
          state_->selection->extent =
              (unsigned)state_->selection->extent < state_->text.size()
                  ? state_->selection->extent + 1
                  : state_->selection->extent;
          if (keyboard->modifiers & mozart::kModifierShift) {
          } else {
            state_->selection->base = state_->selection->extent;
          }

          FTL_VLOG(1) << "Notifying (state = " << *state_ << "')";
          client_->DidUpdateState(state_.Clone(), std::move(event));
        } break;
        case HID_USAGE_KEY_ENTER: {
          client_->OnAction(action_);
        } break;
      }
    }
  }
}

}  // namespace ime
