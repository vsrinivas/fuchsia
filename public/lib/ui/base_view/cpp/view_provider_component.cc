// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/ui/base_view/cpp/view_provider_component.h"

#include "lib/fxl/logging.h"

namespace scenic {

ViewProviderComponent::ViewProviderComponent(
    ViewFactory factory, async::Loop* loop,
    component::StartupContext* startup_context)
    : startup_context_(
          startup_context
              ? std::unique_ptr<component::StartupContext>(startup_context)
              : component::StartupContext::CreateFromStartupInfo()),
      scenic_(startup_context_
                  ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>()),
      service_(startup_context_.get(), scenic_.get(), std::move(factory)) {
  if (startup_context) {
    // We are only responsible for cleaning up the context if we created it
    // ourselves.  In this case we are "borrowing" an existing context that was
    // provided to us, so we shouldn't retain a unique_ptr to it.
    startup_context_.release();
  }

  scenic_.set_error_handler([loop] {
    FXL_LOG(INFO) << "Lost connection to Scenic.";
    loop->Quit();
  });
}

ViewProviderComponent::ViewProviderComponent(
    V1ViewFactory factory, async::Loop* loop,
    component::StartupContext* startup_context)
    : startup_context_(
          startup_context
              ? std::unique_ptr<component::StartupContext>(startup_context)
              : component::StartupContext::CreateFromStartupInfo()),
      scenic_(startup_context_
                  ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>()),
      service_(startup_context_.get(), scenic_.get(), std::move(factory)) {
  // Only retain ownership if we were forced to create a unique StartupContext
  // ourselves.
  if (startup_context) {
    startup_context_.release();
  }

  scenic_.set_error_handler([loop] {
    FXL_LOG(INFO) << "Lost connection to Scenic.";
    loop->Quit();
  });
}

}  // namespace scenic
