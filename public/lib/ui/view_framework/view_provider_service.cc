// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/view_framework/view_provider_service.h"

#include <algorithm>

#include "lib/app/cpp/connect.h"
#include "lib/ftl/logging.h"

namespace mozart {

ViewProviderService::ViewProviderService(
    app::ApplicationContext* application_context,
    ViewFactory view_factory)
    : application_context_(application_context), view_factory_(view_factory) {
  FTL_DCHECK(application_context_);

  application_context_->outgoing_services()->AddService<ViewProvider>(
      [this](fidl::InterfaceRequest<ViewProvider> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

ViewProviderService::~ViewProviderService() {
  application_context_->outgoing_services()->RemoveService<ViewProvider>();
}

void ViewProviderService::CreateView(
    fidl::InterfaceRequest<ViewOwner> view_owner_request,
    fidl::InterfaceRequest<app::ServiceProvider> view_services) {
  ViewContext view_context;
  view_context.application_context = application_context_;
  view_context.view_manager =
      application_context_->ConnectToEnvironmentService<ViewManager>();
  view_context.view_owner_request = std::move(view_owner_request);
  view_context.outgoing_services = std::move(view_services);

  std::unique_ptr<BaseView> view = view_factory_(std::move(view_context));
  if (view) {
    view->SetReleaseHandler([ this, view = view.get() ] {
      auto it = std::find_if(views_.begin(), views_.end(),
                             [view](const std::unique_ptr<BaseView>& other) {
                               return other.get() == view;
                             });
      FTL_DCHECK(it != views_.end());
      views_.erase(it);
    });
    views_.push_back(std::move(view));
  }
}

}  // namespace mozart
