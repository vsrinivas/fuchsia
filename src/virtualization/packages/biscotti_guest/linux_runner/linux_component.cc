// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/packages/biscotti_guest/linux_runner/linux_component.h"

#include <lib/async/default.h>
#include <zircon/status.h>

#include "src/lib/fxl/logging.h"

namespace linux_runner {

// static
std::unique_ptr<LinuxComponent> LinuxComponent::Create(
    TerminationCallback termination_callback, fuchsia::sys::Package package,
    fuchsia::sys::StartupInfo startup_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    fuchsia::ui::app::ViewProviderPtr remote_view_provider) {
  FXL_DCHECK(remote_view_provider) << "Missing remote_view_provider";
  return std::unique_ptr<LinuxComponent>(new LinuxComponent(
      std::move(termination_callback), std::move(package), std::move(startup_info),
      std::move(controller), std::move(remote_view_provider)));
}

LinuxComponent::LinuxComponent(
    TerminationCallback termination_callback, fuchsia::sys::Package package,
    fuchsia::sys::StartupInfo startup_info,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> application_controller_request,
    fuchsia::ui::app::ViewProviderPtr remote_view_provider)
    : termination_callback_(std::move(termination_callback)),
      application_controller_(this),
      remote_view_provider_(std::move(remote_view_provider)) {
  application_controller_.set_error_handler([this](zx_status_t status) { Kill(); });

  auto& launch_info = startup_info.launch_info;
  if (launch_info.directory_request) {
    outgoing_.Serve(std::move(launch_info.directory_request));
  }
  outgoing_.AddPublicService<fuchsia::ui::app::ViewProvider>(view_bindings_.GetHandler(this));
}

LinuxComponent::~LinuxComponent() = default;

// |fuchsia::sys::ComponentController|
void LinuxComponent::Kill() {
  application_controller_.events().OnTerminated(0, fuchsia::sys::TerminationReason::EXITED);

  termination_callback_(this);
  // WARNING: Don't do anything past this point as this instance may have been
  // collected.
}

// |fuchsia::sys::ComponentController|
void LinuxComponent::Detach() { application_controller_.set_error_handler(nullptr); }

// |fuchsia::ui::app::ViewProvider|
void LinuxComponent::CreateView(
    zx::eventpair view_token,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  remote_view_provider_->CreateView(std::move(view_token), std::move(incoming_services),
                                    std::move(outgoing_services));
}

}  // namespace linux_runner
