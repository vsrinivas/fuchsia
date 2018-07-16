// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/ime/app.h"

#include <algorithm>

#include <fuchsia/ui/input/cpp/fidl.h>
#include "garnet/bin/ui/ime/ime_impl.h"
#include "lib/component/cpp/connect.h"
#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"

namespace ime {

App::App(const fxl::CommandLine& command_line)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()) {
  FXL_DCHECK(startup_context_);
  startup_context_->outgoing().AddPublicService(ime_bindings_.GetHandler(this));
}

App::~App() {}

void App::GetInputMethodEditor(
    fuchsia::ui::input::KeyboardType keyboard_type,
    fuchsia::ui::input::InputMethodAction action,
    fuchsia::ui::input::TextInputState initial_state,
    fidl::InterfaceHandle<fuchsia::ui::input::InputMethodEditorClient> client,
    fidl::InterfaceRequest<fuchsia::ui::input::InputMethodEditor>
        editor_request) {
  FXL_DCHECK(client);
  FXL_DCHECK(editor_request.is_valid());

  FXL_VLOG(1)
      << "GetInputMethodEditor: "
      << ", keyboard_type="
      << static_cast<
             std::underlying_type<fuchsia::ui::input::KeyboardType>::type>(
             keyboard_type)
      << ", action="
      << static_cast<
             std::underlying_type<fuchsia::ui::input::InputMethodAction>::type>(
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
