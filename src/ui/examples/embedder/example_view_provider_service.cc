// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/embedder/example_view_provider_service.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

namespace embedder {

ExampleViewProviderService::ExampleViewProviderService(sys::ComponentContext* component_ctx,
                                                       ViewFactory factory)
    : component_ctx_(component_ctx), view_factory_fn_(std::move(factory)) {
  FX_DCHECK(component_ctx_);

  component_ctx->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        bindings_.AddBinding(this, std::move(request));
      },
      "view_provider");
}

ExampleViewProviderService::~ExampleViewProviderService() {
  component_ctx_->outgoing()->RemovePublicService<fuchsia::ui::app::ViewProvider>();
}

void ExampleViewProviderService::CreateView(
    zx::eventpair token, fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  // Services in CreateView() API are from the client's perspective.
  // Services in the ViewContext are from the view's perspective.
  ViewContext view_ctx{
      .component_context = component_ctx_,
      .token = scenic::ToViewToken(std::move(token)),
      .incoming_services = std::move(outgoing_services),
      .outgoing_services = std::move(incoming_services),
  };
  view_factory_fn_(std::move(view_ctx));
}

}  // namespace embedder
