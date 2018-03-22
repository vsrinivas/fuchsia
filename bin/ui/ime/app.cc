// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/ime/app.h"

#include <algorithm>

#include <fuchsia/cpp/input.h>
#include "garnet/bin/ui/ime/ime_impl.h"
#include "lib/app/cpp/connect.h"
#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"

namespace ime {

App::App(const fxl::CommandLine& command_line)
    : application_context_(
          component::ApplicationContext::CreateFromStartupInfo()) {
  FXL_DCHECK(application_context_);
  application_context_->outgoing_services()->AddService<input::ImeService>(
      [this](fidl::InterfaceRequest<input::ImeService> request) {
        ime_bindings_.AddBinding(this, std::move(request));
      });
}

App::~App() {}

void App::GetInputMethodEditor(
    input::KeyboardType keyboard_type,
    input::InputMethodAction action,
    input::TextInputState initial_state,
    fidl::InterfaceHandle<input::InputMethodEditorClient> client,
    fidl::InterfaceRequest<input::InputMethodEditor> editor_request) {
  FXL_DCHECK(client);
  FXL_DCHECK(editor_request.is_valid());

  FXL_VLOG(1)
      << "GetInputMethodEditor: "
      << ", keyboard_type="
      << static_cast<std::underlying_type<input::KeyboardType>::type>(
             keyboard_type)
      << ", action="
      << static_cast<std::underlying_type<input::InputMethodAction>::type>(
             action)
      << ", initial_state=" << &initial_state;

  std::unique_ptr<ImeImpl> ime_impl =
      std::make_unique<ImeImpl>(keyboard_type, action, std::move(initial_state),
                                std::move(client), std::move(editor_request));
  // FIXME(jpoichet) we're leaking
  ime_.emplace(ime_.end(), std::move(ime_impl));
}

void App::OnImeDisconnected(ImeImpl* ime) {}

}  // namespace ime
