// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_PRESENTATION_CONTAINER_H_
#define SRC_MODULAR_BIN_BASEMGR_PRESENTATION_CONTAINER_H_

#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/lib/fxl/macros.h"

namespace modular {

// This class holds the presentation service.
class PresentationContainer {
 public:
  // Initializes the presentation with the given |view_owner| and
  // |shell_config| and connects it to the presenter service.
  explicit PresentationContainer(fuchsia::ui::policy::Presenter* const presenter,
                                 fuchsia::ui::views::ViewHolderToken view_holder_token,
                                 fuchsia::ui::views::ViewRef view_ref);

  ~PresentationContainer();

  // Connects the given |request| to the presentation service. NOTE that the
  // presentation needs to be connected to the presenter service in order for
  // this presentation to display on UI.
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request);

 private:
  // Used to initialize the presentation. Not owned.
  fuchsia::ui::policy::Presenter* const presenter_;

  struct {
    // Presentation service, which is binded to the |presenter_| on
    // initialization.
    fuchsia::ui::policy::PresentationPtr presentation;
    // Bindings to the presentation service, which is binded to the client on
    // |GetPresentation|.
    fidl::BindingSet<fuchsia::ui::policy::Presentation> bindings;
  } presentation_state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PresentationContainer);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_PRESENTATION_CONTAINER_H_
