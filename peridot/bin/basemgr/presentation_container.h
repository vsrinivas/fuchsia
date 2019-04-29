// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_PRESENTATION_CONTAINER_H_
#define PERIDOT_BIN_BASEMGR_PRESENTATION_CONTAINER_H_

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <src/lib/fxl/macros.h>

namespace modular {

// This class holds the presentation service.
class PresentationContainer : fuchsia::ui::policy::KeyboardCaptureListenerHACK {
 public:
  // Initializes the presentation with the given |view_owner| and
  // |shell_config| and connects it to the presenter service. Attaches
  // the given |on_swap_session_shell| to a reserved keyboard binding
  // (this is a hack to keep SwapSessionShell working).
  explicit PresentationContainer(
      fuchsia::ui::policy::Presenter* const presenter,
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fuchsia::modular::session::SessionShellConfig shell_config,
      fit::function<void()> on_swap_session_shell);

  ~PresentationContainer() override;

  // Connects the given |request| to the presentation service. NOTE that the
  // presentation needs to be connected to the presenter service in order for
  // this presentation to display on UI.
  void GetPresentation(
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request);

 private:
  // Adds global keyboard shortcuts, including a reserved keyboard binding.
  void AddGlobalKeyboardShortcuts(
      fuchsia::ui::policy::PresentationPtr& presentation);

  // |fuchsia::ui::policy::KeyboardCaptureListenerHACK|
  void OnEvent(fuchsia::ui::input::KeyboardEvent event) override;

  void SetNextShadowTechnique();

  void SetShadowTechnique(fuchsia::ui::gfx::ShadowTechnique shadow_technique);

  void ToggleClipping();

  // Used to initialize the presentation. Not owned.
  fuchsia::ui::policy::Presenter* const presenter_;

  // Bindings for global keyboard shortcuts. See |AddGlobalKeyboardShortcuts|.
  fidl::BindingSet<fuchsia::ui::policy::KeyboardCaptureListenerHACK>
      keyboard_capture_listener_bindings_;
  // Callback invoked when keyboard binding for swapping session shells is
  // observed.
  fit::function<void()> on_swap_session_shell_;

  struct {
    // Presentation service, which is binded to the |presenter_| on
    // initialization.
    fuchsia::ui::policy::PresentationPtr presentation;
    // Bindings to the presentation service, which is binded to the client on
    // |GetPresentation|.
    fidl::BindingSet<fuchsia::ui::policy::Presentation> bindings;

    // Scenic demos for toggling a presentation's shadow technique and clipping.
    fuchsia::ui::gfx::ShadowTechnique shadow_technique =
        fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED;
    bool clipping_enabled{};
  } presentation_state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PresentationContainer);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_PRESENTATION_CONTAINER_H_
