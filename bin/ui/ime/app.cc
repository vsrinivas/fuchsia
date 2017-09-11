// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/ime/app.h"

#include <algorithm>

#include "lib/app/cpp/connect.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/input/fidl/text_input.fidl.h"
#include "garnet/bin/ui/ime/ime_impl.h"
#include "lib/ftl/logging.h"

namespace ime {

App::App(const ftl::CommandLine& command_line)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  FTL_DCHECK(application_context_);
  application_context_->outgoing_services()->AddService<mozart::ImeService>(
      [this](fidl::InterfaceRequest<mozart::ImeService> request) {
        ime_bindings_.AddBinding(this, std::move(request));
      });
}

App::~App() {}

void App::GetInputMethodEditor(
    mozart::KeyboardType keyboard_type,
    mozart::InputMethodAction action,
    mozart::TextInputStatePtr initial_state,
    fidl::InterfaceHandle<mozart::InputMethodEditorClient> client,
    fidl::InterfaceRequest<mozart::InputMethodEditor> editor_request) {
  FTL_DCHECK(initial_state);
  FTL_DCHECK(client);
  FTL_DCHECK(editor_request.is_pending());

  FTL_VLOG(1) << "GetInputMethodEditor: "
              << ", keyboard_type=" << keyboard_type << ", action=" << action
              << ", initial_state=" << *initial_state;

  std::unique_ptr<ImeImpl> ime_impl =
      std::make_unique<ImeImpl>(keyboard_type, action, std::move(initial_state),
                                std::move(client), std::move(editor_request));
  // FIXME(jpoichet) we're leaking
  ime_.emplace(ime_.end(), std::move(ime_impl));
}

void App::OnImeDisconnected(ImeImpl* ime) {}

}  // namespace ime
