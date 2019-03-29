// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/embedder/example_view_provider_service.h"

#include <src/lib/fxl/logging.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

namespace embedder {

ExampleViewProviderService::ExampleViewProviderService(
    component::StartupContext* startup_ctx, ViewFactory factory)
    : startup_ctx_(startup_ctx), view_factory_fn_(std::move(factory)) {
  FXL_DCHECK(startup_ctx_);

  startup_ctx->outgoing()
      .deprecated_services()
      ->AddService<fuchsia::ui::app::ViewProvider>(
          [this](
              fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
            bindings_.AddBinding(this, std::move(request));
          },
          "view_provider");
}

ExampleViewProviderService::~ExampleViewProviderService() {
  startup_ctx_->outgoing()
      .deprecated_services()
      ->RemoveService<fuchsia::ui::app::ViewProvider>();
}

void ExampleViewProviderService::CreateView(
    ::zx::eventpair token,
    ::fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    ::fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  // Services in CreateView() API are from the client's perspective.
  // Services in the ViewContext are from the view's perspective.
  ViewContext view_ctx{
      .startup_context = startup_ctx_,
      .token = scenic::ToViewToken(std::move(token)),
      .incoming_services = std::move(outgoing_services),
      .outgoing_services = std::move(incoming_services),
  };
  view_factory_fn_(std::move(view_ctx));
}

}  // namespace embedder
