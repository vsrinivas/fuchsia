// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/present_view.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include "src/lib/fxl/strings/substitute.h"

namespace present_view {
namespace {

// The component manifest of the component that provides an implementation of
// fuchsia.intl.PropertyProvider.  present_view will start it up if the flag
// --locale=... is specified.
constexpr char kIntlPropertyProviderUri[] =
    "fuchsia-pkg://fuchsia.com/intl_property_manager#meta/"
    "intl_property_manager_without_flags.cmx";

}  // anonymous namespace

PresentView::PresentView(std::unique_ptr<sys::ComponentContext> context,
                         ViewErrorCallback on_view_error)
    : context_(std::move(context)), view_error_callback_(std::move(on_view_error)) {}

PresentView::~PresentView() = default;

bool PresentView::Present(ViewInfo view_info) {
  FX_CHECK(!intl_controller_);
  FX_CHECK(!presenter_);
  FX_CHECK(!view_controller_);

  // Skip doing any work for an empty URL.
  if (view_info.url.empty()) {
    return false;
  }

  fuchsia::sys::LauncherPtr launcher = context_->svc()->Connect<fuchsia::sys::Launcher>();
  launcher.set_error_handler([this](zx_status_t status) {
    Kill();
    view_error_callback_("fuchsia::sys::Launcher error", status);
  });

  // Configure the information to launch the component with.
  fuchsia::sys::LaunchInfo launch_info;
  fidl::InterfaceHandle<fuchsia::io::Directory> outgoing_services_dir;
  launch_info.directory_request = outgoing_services_dir.NewRequest().TakeChannel();
  launch_info.url = std::move(view_info.url);
  launch_info.arguments = fidl::VectorPtr(std::move(view_info.arguments));

  if (!view_info.locale.empty()) {
    fidl::InterfaceHandle<fuchsia::io::Directory> additional_services_dir;

    // If we wanted present_view to serve |fuchsia.intl.ProfileProvider|, then start the
    // |intl_property_provider| and make it available to the component under test.
    RunIntlService(view_info.locale, additional_services_dir.NewRequest(), launcher.get());

    fuchsia::sys::ServiceListPtr injected_services(new fuchsia::sys::ServiceList);
    injected_services->names.push_back(fuchsia::intl::PropertyProvider::Name_);
    injected_services->host_directory = additional_services_dir.TakeChannel();
    launch_info.additional_services = std::move(injected_services);
  }

  // Launch the component.
  launcher->CreateComponent(std::move(launch_info), view_controller_.NewRequest());
  view_controller_.set_error_handler([this](zx_status_t status) {
    Kill();
    view_error_callback_("fuchsia::sys::ComponentController (for View) error", status);
  });

  // Instruct the component to create a Scenic View using the |fuchsia.ui.app.ViewProvider|
  // service that it exposes.
  auto services = std::make_shared<sys::ServiceDirectory>(std::move(outgoing_services_dir));
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  if (!view_info.locale.empty()) {
    // For now, use the presence of a locale option as an indication to use the
    // |fuchsia.ui.views.View| interface.
    view_ = services->Connect<fuchsia::ui::views::View>();
    view_.set_error_handler([this](zx_status_t status) {
      Kill();
      view_error_callback_("fuchsia::ui::views::View error", status);
    });
    view_->Present(std::move(view_token));
  } else {
    legacy_view_provider_ = services->Connect<fuchsia::ui::app::ViewProvider>();
    legacy_view_provider_.set_error_handler([this](zx_status_t status) {
      Kill();
      view_error_callback_("fuchsia::ui::app::ViewProvider error", status);
    });
    legacy_view_provider_->CreateView(std::move(view_token.value), nullptr, nullptr);
  }

  // Ask the presenter to display it.
  presenter_ = context_->svc()->Connect<fuchsia::ui::policy::Presenter>();
  presenter_.set_error_handler([this](zx_status_t status) {
    Kill();
    view_error_callback_("fuchsia::ui::policy::Presenter error", status);
  });
  presenter_->PresentView(std::move(view_holder_token), nullptr);

  return true;
}

void PresentView::Kill() {
  presenter_.Unbind();
  legacy_view_provider_.Unbind();
  view_.Unbind();
  view_controller_.Unbind();
  intl_controller_.Unbind();
}

void PresentView::RunIntlService(const std::string& locale,
                                 fidl::InterfaceRequest<fuchsia::io::Directory> directory_request,
                                 fuchsia::sys::Launcher* launcher) {
  FX_CHECK(!intl_controller_);

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kIntlPropertyProviderUri;
  launch_info.arguments = std::vector{std::string{"--set_initial_profile"},
                                      std::string{fxl::Substitute("--locale_ids=$0", locale)}};
  launch_info.directory_request = directory_request.TakeChannel();
  launcher->CreateComponent(std::move(launch_info), intl_controller_.NewRequest());
  intl_controller_.set_error_handler([this](zx_status_t status) {
    Kill();
    view_error_callback_("fuchsia::sys::ComponentController (for intl_manager) error", status);
  });
}

}  // namespace present_view
