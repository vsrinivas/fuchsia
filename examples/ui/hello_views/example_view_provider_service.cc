// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_views/example_view_provider_service.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/zx/eventpair.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace hello_views {

ExampleViewProviderService::ExampleViewProviderService(
    fuchsia::sys::StartupContext* startup_ctx, ViewFactory factory)
    : startup_ctx_(startup_ctx), view_factory_fn_(factory) {
  FXL_DCHECK(startup_ctx_);

  startup_ctx->outgoing_services()->AddService<fuchsia::ui::app::ViewProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        bindings_.AddBinding(this, std::move(request));
      },
      "view_provider");
}

ExampleViewProviderService::~ExampleViewProviderService() {
  startup_ctx_->outgoing_services()
      ->RemoveService<fuchsia::ui::app::ViewProvider>();
}

// |ui::ViewProvider|
void ExampleViewProviderService::CreateView(
    ::zx::eventpair token,
    ::fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    ::fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  // Services in CreateView() API are from the client's perspective.
  // Services in the ViewContext are from the view's perspective.
  ViewContext view_ctx{
      .startup_context = startup_ctx_,
      .token = std::move(token),
      .incoming_services = std::move(outgoing_services),
      .outgoing_services = std::move(incoming_services),
  };
  view_factory_fn_(std::move(view_ctx));
}

}  // namespace hello_views
