// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "lib/component/cpp/connect.h"
#include "lib/ui/base_view/cpp/view_provider_service_transitional.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"
#include "src/lib/fxl/logging.h"

namespace scenic {

ViewProviderServiceTransitional::ViewProviderServiceTransitional(
    sys::ComponentContext* component_context, fuchsia::ui::scenic::Scenic* scenic,
    ViewFactoryTransitional view_factory)
    : ViewProviderServiceTransitional(component_context, scenic) {
  FXL_DCHECK(view_factory);
  view_factory_ = std::move(view_factory);
}

ViewProviderServiceTransitional::ViewProviderServiceTransitional(
    sys::ComponentContext* component_context, fuchsia::ui::scenic::Scenic* scenic)
    : component_context_(component_context), scenic_(scenic) {
  FXL_DCHECK(component_context_);

  component_context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      bindings_.GetHandler(this));
}

ViewProviderServiceTransitional::~ViewProviderServiceTransitional() {
  component_context_->outgoing()->RemovePublicService<fuchsia::ui::app::ViewProvider>();
}

void ViewProviderServiceTransitional::CreateView(
    zx::eventpair view_token,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  ViewContextTransitional context = {
      .session_and_listener_request = CreateScenicSessionPtrAndListenerRequest(scenic_),
      .view_token = scenic::ToViewToken(std::move(view_token)),
      .incoming_services = std::move(incoming_services),
      .outgoing_services = std::move(outgoing_services),
      .component_context = component_context_,
  };

  if (view_factory_) {
    if (auto base_view = view_factory_(std::move(context))) {
      base_view->SetReleaseHandler([this, base_view = base_view.get()](zx_status_t status) {
        auto it = std::find_if(views_.begin(), views_.end(),
                               [base_view](const std::unique_ptr<BaseViewTransitional>& other) {
                                 return other.get() == base_view;
                               });
        FXL_DCHECK(it != views_.end());
        views_.erase(it);
      });
      views_.push_back(std::move(base_view));
    }
  } else {
    // Should never happen; this was already checked in the constructors.
    FXL_DCHECK(false) << "No ViewFactory found";
  }
}

}  // namespace scenic
