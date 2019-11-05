// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/present_view.h"

#include <lib/fidl/cpp/vector.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "src/lib/fxl/logging.h"

namespace present_view {

PresentView::PresentView(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {}

bool PresentView::Present(ViewInfo view_info, fit::function<void(zx_status_t)> on_view_error) {
  if (view_info.url.empty()) {
    FXL_LOG(ERROR) << "present_view requires the url of a view provider application "
                      "to present_view.";
    return false;
  }

  // Configure the information to launch the component with.
  fuchsia::sys::LaunchInfo launch_info;
  std::shared_ptr<sys::ServiceDirectory> services =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launch_info.url = std::move(view_info.url);
  launch_info.arguments = fidl::VectorPtr(std::move(view_info.arguments));

  // Launch the component.
  auto launcher = context_->svc()->Connect<fuchsia::sys::Launcher>();
  launcher->CreateComponent(std::move(launch_info), view_controller_.NewRequest());
  view_controller_.set_error_handler(std::move(on_view_error));

  // Instruct the component to create a scenic View using one of the available
  // interfaces.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  if (!view_info.locale.empty()) {
    // For now, use the presence of a locale option as an indication to use the
    // |fuchsia.ui.views.View| interface.
    view_ = services->Connect<fuchsia::ui::views::View>();
    view_->Present(std::move(view_token));
    // TODO(I18N-13): Provide fuchsia.intl.PropertyProvider instance.
  } else {
    legacy_view_provider_ = services->Connect<fuchsia::ui::app::ViewProvider>();
    legacy_view_provider_->CreateView(std::move(view_token.value), nullptr, nullptr);
  }

  // Ask the presenter to display it.
  presenter_ = context_->svc()->Connect<fuchsia::ui::policy::Presenter>();
  presenter_->PresentView(std::move(view_holder_token), nullptr);

  return true;
}

}  // namespace present_view
