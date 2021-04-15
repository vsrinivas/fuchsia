// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/presentation_container.h"

#include <lib/syslog/cpp/macros.h>

#include <cmath>

namespace modular {

PresentationContainer::PresentationContainer(fuchsia::ui::policy::Presenter* const presenter,
                                             fuchsia::ui::views::ViewHolderToken view_holder_token,
                                             fuchsia::ui::views::ViewRef view_ref)
    : presenter_(presenter) {
  presenter_->PresentOrReplaceView2(std::move(view_holder_token), std::move(view_ref),
                                    presentation_state_.presentation.NewRequest());
}

PresentationContainer::~PresentationContainer() = default;

void PresentationContainer::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  presentation_state_.bindings.AddBinding(presentation_state_.presentation.get(),
                                          std::move(request));
}

}  // namespace modular
