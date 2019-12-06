// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/presentation_container.h"

#include <cmath>

#include "src/lib/fxl/logging.h"

namespace modular {

namespace {
// Presentation controller will bind |on_swap_session_shell_|
// to lCtrl + |kSwapSessionShellKeyboardBinding| on the active presentation.
constexpr char kSwapSessionShellKeyboardBinding = ' ';
}  // namespace

PresentationContainer::PresentationContainer(
    fuchsia::ui::policy::Presenter* const presenter,
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fuchsia::modular::session::SessionShellConfig shell_config,
    fit::function<void()> on_swap_session_shell)
    : presenter_(presenter), on_swap_session_shell_(std::move(on_swap_session_shell)) {
  presenter_->PresentOrReplaceView(std::move(view_holder_token),
                                   presentation_state_.presentation.NewRequest());
  AddGlobalKeyboardShortcuts(presentation_state_.presentation);
}

PresentationContainer::~PresentationContainer() = default;

void PresentationContainer::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  presentation_state_.bindings.AddBinding(presentation_state_.presentation.get(),
                                          std::move(request));
}

void PresentationContainer::AddGlobalKeyboardShortcuts(
    fuchsia::ui::policy::PresentationPtr& presentation) {
  presentation->CaptureKeyboardEventHACK(
      {
          .code_point = kSwapSessionShellKeyboardBinding,
          .modifiers = fuchsia::ui::input::kModifierLeftControl,
      },
      keyboard_capture_listener_bindings_.AddBinding(this));
  presentation->CaptureKeyboardEventHACK(
      {
          .code_point = 's',
          .modifiers = fuchsia::ui::input::kModifierLeftControl,
      },
      keyboard_capture_listener_bindings_.AddBinding(this));
  presentation->CaptureKeyboardEventHACK(
      {
          .code_point = 'l',
          .modifiers = fuchsia::ui::input::kModifierRightAlt,
      },
      keyboard_capture_listener_bindings_.AddBinding(this));
}

void PresentationContainer::OnEvent(fuchsia::ui::input::KeyboardEvent event) {
  switch (event.code_point) {
    case kSwapSessionShellKeyboardBinding: {
      if (on_swap_session_shell_) {
        on_swap_session_shell_();
      }
      break;
    }
    default:
      FXL_DLOG(INFO) << "Unknown keyboard event: codepoint=" << event.code_point
                     << ", modifiers=" << event.modifiers;
      break;
  }
}

}  // namespace modular
