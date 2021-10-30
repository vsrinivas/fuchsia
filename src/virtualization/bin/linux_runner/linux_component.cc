// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/linux_runner/linux_component.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace linux_runner {

// static
std::unique_ptr<LinuxComponent> LinuxComponent::Create(
    TerminationCallback termination_callback, fuchsia::sys::Package package,
    zx::channel directory_request,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> app_controller,
    fuchsia::sys::ComponentControllerPtr remote_controller,
    fuchsia::ui::app::ViewProviderPtr remote_view_provider, uint32_t id) {
  FX_DCHECK(remote_view_provider) << "Missing remote_view_provider";
  return std::unique_ptr<LinuxComponent>(
      new LinuxComponent(std::move(termination_callback), std::move(package),
                         std::move(directory_request), std::move(app_controller),
                         std::move(remote_controller), std::move(remote_view_provider), id));
}

LinuxComponent::LinuxComponent(
    TerminationCallback termination_callback, fuchsia::sys::Package package,
    zx::channel directory_request,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> application_controller_request,
    fuchsia::sys::ComponentControllerPtr remote_controller,
    fuchsia::ui::app::ViewProviderPtr remote_view_provider, uint32_t id)
    : termination_callback_(std::move(termination_callback)),
      application_controller_(this),
      remote_controller_(std::move(remote_controller)),
      remote_view_provider_(std::move(remote_view_provider)),
      id_(id) {
  application_controller_.Bind(std::move(application_controller_request));
  application_controller_.set_error_handler([this](zx_status_t status) { Kill(); });

  remote_controller_.set_error_handler([this](zx_status_t /*status*/) {
    HandleOnTerminated(0, fuchsia::sys::TerminationReason::UNKNOWN);
  });
  remote_controller_.events().OnTerminated =
      fit::bind_member(this, &LinuxComponent::HandleOnTerminated);

  if (directory_request) {
    outgoing_.Serve(std::move(directory_request));
  }
  outgoing_.AddPublicService<fuchsia::ui::app::ViewProvider>(view_bindings_.GetHandler(this));
}

LinuxComponent::~LinuxComponent() = default;

void LinuxComponent::HandleOnTerminated(int64_t return_code,
                                        fuchsia::sys::TerminationReason reason) {
  application_controller_.events().OnTerminated(return_code, reason);

  termination_callback_(id_);
  // WARNING: Don't do anything past this point as this instance may have been
  // collected.
}

// |fuchsia::sys::ComponentController|
void LinuxComponent::Kill() {
  remote_controller_->Kill();
  application_controller_.events().OnTerminated(0, fuchsia::sys::TerminationReason::EXITED);

  termination_callback_(id_);
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

// |fuchsia::ui::app::ViewProvider|
void LinuxComponent::CreateViewWithViewRef(zx::eventpair token,
                                           fuchsia::ui::views::ViewRefControl view_ref_control,
                                           fuchsia::ui::views::ViewRef view_ref) {
  remote_view_provider_->CreateViewWithViewRef(std::move(token), std::move(view_ref_control),
                                               std::move(view_ref));
}

// |fuchsia::ui::app::ViewProvider|
void LinuxComponent::CreateView2(fuchsia::ui::app::CreateView2Args args) {
  remote_view_provider_->CreateView2(std::move(args));
}

}  // namespace linux_runner
