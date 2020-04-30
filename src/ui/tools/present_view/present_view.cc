// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/present_view.h"

#include <lib/fidl/cpp/vector.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/substitute.h"

namespace {

// The component manifest of the component that provides an implementation of
// fuchsia.intl.PropertyProvider.  present_view will start it up if the flag
// --locale=... is specified.
constexpr char kIntlPropertyProviderManifest[] =
    "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cmx";

}  // namespace

namespace present_view {

PresentView::PresentView(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {}

bool PresentView::Present(ViewInfo view_info, fit::function<void(zx_status_t)> on_view_error) {
  if (view_info.url.empty()) {
    FX_LOGS(ERROR) << "present_view requires the url of a view provider application "
                      "to present_view.";
    return false;
  }

  fuchsia::sys::LauncherPtr launcher = context_->svc()->Connect<fuchsia::sys::Launcher>();

  // Configure the information to launch the component with.
  fuchsia::sys::LaunchInfo launch_info;
  std::shared_ptr<sys::ServiceDirectory> services =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
  launch_info.url = std::move(view_info.url);
  launch_info.arguments = fidl::VectorPtr(std::move(view_info.arguments));

  zx::channel server_side, client_side;
  if (!view_info.locale.empty()) {
    // If we wanted present_view to serve |fuchsia.intl.ProfileProvider|, then start the
    // |intl_property_provider| and make it available to the component under test.
    //
    FX_CHECK(ZX_OK == zx::channel::create(0, &server_side, &client_side));
    RunIntlService(view_info.locale, std::move(server_side), &launcher);

    fuchsia::sys::ServiceListPtr injected_services(new fuchsia::sys::ServiceList);
    injected_services->names.push_back(fuchsia::intl::PropertyProvider::Name_);
    injected_services->host_directory = std::move(client_side);
    launch_info.additional_services = std::move(injected_services);
  }

  // Launch the component.
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
  } else {
    legacy_view_provider_ = services->Connect<fuchsia::ui::app::ViewProvider>();
    legacy_view_provider_->CreateView(std::move(view_token.value), nullptr, nullptr);
  }

  // Ask the presenter to display it.
  presenter_ = context_->svc()->Connect<fuchsia::ui::policy::Presenter>();
  presenter_->PresentView(std::move(view_holder_token), nullptr);

  return true;
}

void PresentView::RunIntlService(const std::string& locale, zx::channel server_side,
                                 fuchsia::sys::LauncherPtr* launcher) {
  FX_LOGS(INFO) << "Starting intl property provider with locale: " << locale;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kIntlPropertyProviderManifest;
  launch_info.arguments.emplace();
  launch_info.arguments->push_back("--set_initial_profile");
  launch_info.arguments->push_back(fxl::Substitute("--locale_ids=$0", locale));
  launch_info.directory_request = std::move(server_side);
  (*launcher)->CreateComponent(std::move(launch_info), view_controller_.NewRequest());
}

}  // namespace present_view
