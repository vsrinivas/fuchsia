// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/basemgr/presentation_container.h"

#include <src/lib/fxl/logging.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <cmath>

#include "peridot/bin/basemgr/session_shell_settings/session_shell_settings.h"

namespace modular {

namespace {
// Presentation controller will bind |on_swap_session_shell_|
// to lCtrl + |kSwapSessionShellKeyboardBinding| on the active presentation.
constexpr char kSwapSessionShellKeyboardBinding = ' ';
}  // namespace

PresentationContainer::PresentationContainer(
    fuchsia::ui::policy::Presenter* const presenter,
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fuchsia::modular::internal::SessionShellConfig shell_config,
    fit::function<void()> on_swap_session_shell)
    : presenter_(presenter),
      on_swap_session_shell_(std::move(on_swap_session_shell)) {
  presenter_->PresentView(std::move(view_holder_token),
                          presentation_state_.presentation.NewRequest());
  AddGlobalKeyboardShortcuts(presentation_state_.presentation);

  SetShadowTechnique(presentation_state_.shadow_technique);

  // Set the presentation of the given view to the settings of the active
  // session shell.
  if (shell_config.display_usage() !=
      fuchsia::ui::policy::DisplayUsage::kUnknown) {
    FXL_DLOG(INFO) << "Setting display usage: "
                   << fidl::ToUnderlying(shell_config.display_usage());
    presentation_state_.presentation->SetDisplayUsage(
        shell_config.display_usage());
  }

  if (!std::isnan(shell_config.screen_width()) &&
      !std::isnan(shell_config.screen_height())) {
    FXL_DLOG(INFO) << "Setting display size: " << shell_config.screen_width()
                   << " x " << shell_config.screen_height();
    presentation_state_.presentation->SetDisplaySizeInMm(
        shell_config.screen_width(), shell_config.screen_height());
  }
}

PresentationContainer::~PresentationContainer() = default;

void PresentationContainer::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  presentation_state_.bindings.AddBinding(
      presentation_state_.presentation.get(), std::move(request));
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
    case 's': {
      SetNextShadowTechnique();
      break;
    }
    case 'l':
      ToggleClipping();
      break;
    default:
      FXL_DLOG(INFO) << "Unknown keyboard event: codepoint=" << event.code_point
                     << ", modifiers=" << event.modifiers;
      break;
  }
}

void PresentationContainer::SetNextShadowTechnique() {
  using ShadowTechnique = fuchsia::ui::gfx::ShadowTechnique;

  // These are the only two types currently supported by the Scenic renderer.
  // TODO(MF-266) Make this more robust.
  auto next_shadow_technique =
      [](ShadowTechnique shadow_technique) -> ShadowTechnique {
    switch (shadow_technique) {
      case ShadowTechnique::UNSHADOWED:
        return ShadowTechnique::STENCIL_SHADOW_VOLUME;
      case ShadowTechnique::STENCIL_SHADOW_VOLUME:
        return ShadowTechnique::UNSHADOWED;
      default:
        FXL_LOG(ERROR) << "Unknown shadow technique: "
                       << fidl::ToUnderlying(shadow_technique);
        return ShadowTechnique::UNSHADOWED;
    }
  };

  SetShadowTechnique(
      next_shadow_technique(presentation_state_.shadow_technique));
}

void PresentationContainer::SetShadowTechnique(
    fuchsia::ui::gfx::ShadowTechnique shadow_technique) {
  if (!presentation_state_.presentation)
    return;

  presentation_state_.shadow_technique = shadow_technique;

  FXL_LOG(INFO) << "Setting shadow technique to "
                << fidl::ToUnderlying(presentation_state_.shadow_technique);

  fuchsia::ui::gfx::RendererParam param;
  param.set_shadow_technique(presentation_state_.shadow_technique);

  std::vector<fuchsia::ui::gfx::RendererParam> renderer_params;
  renderer_params.push_back(std::move(param));

  presentation_state_.presentation->SetRendererParams(
      std::move(renderer_params));
}

void PresentationContainer::ToggleClipping() {
  if (!presentation_state_.presentation)
    return;

  FXL_DLOG(INFO) << "Toggling clipping";

  presentation_state_.clipping_enabled = !presentation_state_.clipping_enabled;
  presentation_state_.presentation->EnableClipping(
      presentation_state_.clipping_enabled);
}

}  // namespace modular
