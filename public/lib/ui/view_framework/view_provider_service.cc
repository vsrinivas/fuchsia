// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/view_framework/view_provider_service.h"

#include <algorithm>

#include "lib/component/cpp/connect.h"
#include "lib/fxl/logging.h"

namespace mozart {

ViewProviderService::ViewProviderService(
    component::StartupContext* startup_context, ViewFactory view_factory)
    : startup_context_(startup_context), view_factory_(view_factory) {
  FXL_DCHECK(startup_context_);

  startup_context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

ViewProviderService::~ViewProviderService() {
  startup_context_->outgoing().RemovePublicService<ViewProvider>();
}

void ViewProviderService::CreateView(
    fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
        view_owner_request,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> view_services) {
  ViewContext view_context;
  view_context.startup_context = startup_context_;
  view_context.view_manager =
      startup_context_
          ->ConnectToEnvironmentService<::fuchsia::ui::views_v1::ViewManager>();
  view_context.view_owner_request = std::move(view_owner_request);
  view_context.outgoing_services = std::move(view_services);

  std::unique_ptr<BaseView> view = view_factory_(std::move(view_context));
  if (view) {
    view->SetReleaseHandler([this, view = view.get()] {
      auto it = std::find_if(views_.begin(), views_.end(),
                             [view](const std::unique_ptr<BaseView>& other) {
                               return other.get() == view;
                             });
      FXL_DCHECK(it != views_.end());
      views_.erase(it);
    });
    views_.push_back(std::move(view));
  }
}

}  // namespace mozart
