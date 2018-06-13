// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/cpp/view_provider_service.h"

#include <algorithm>

#include "lib/app/cpp/connect.h"
#include "lib/fxl/logging.h"

namespace scenic {

ViewProviderService::ViewProviderService(
    fuchsia::sys::StartupContext* startup_context,
    fuchsia::ui::scenic::Scenic* scenic, ViewFactory view_factory)
    : startup_context_(startup_context),
      scenic_(scenic),
      view_factory_(view_factory) {
  FXL_DCHECK(startup_context_);

  startup_context_->outgoing().AddPublicService<ViewProvider>(
      [this](fidl::InterfaceRequest<ViewProvider> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

ViewProviderService::~ViewProviderService() {
  startup_context_->outgoing().RemovePublicService<ViewProvider>();
}

void ViewProviderService::CreateView(
    zx::eventpair view_token,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  ViewFactoryArgs args = {
      .session_and_listener_request =
          CreateScenicSessionPtrAndListenerRequest(scenic_),
      .view_token = std::move(view_token),
      .incoming_services = std::move(incoming_services),
      .outgoing_services = std::move(outgoing_services),
      .startup_context = startup_context_,
  };

  std::unique_ptr<BaseView> view = view_factory_(std::move(args));
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

}  // namespace scenic
