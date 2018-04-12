// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/guest_environment_impl.h"

#include "lib/fxl/logging.h"

namespace guestmgr {

static uint32_t g_next_guest_id = 0;

GuestEnvironmentImpl::GuestEnvironmentImpl(
    component::ApplicationContext* context,
    const std::string& label,
    fidl::InterfaceRequest<guest::GuestEnvironment> request)
    : context_(context) {
  CreateApplicationEnvironment(label);
  bindings_.AddBinding(this, std::move(request));
}
GuestEnvironmentImpl::~GuestEnvironmentImpl() = default;

std::vector<GuestHolder*> GuestEnvironmentImpl::guests() const {
  std::vector<GuestHolder*> guests;
  for (const auto& guest : guests_) {
    guests.push_back(guest.first);
  }
  return guests;
}

void GuestEnvironmentImpl::LaunchGuest(
    guest::GuestLaunchInfo launch_info,
    fidl::InterfaceRequest<guest::GuestController> controller) {
  component::Services guest_services;
  component::ApplicationControllerPtr guest_app_controller;
  component::ApplicationLaunchInfo guest_launch_info;
  guest_launch_info.url = launch_info.url;
  guest_launch_info.arguments = std::move(launch_info.vmm_args);
  guest_launch_info.directory_request = guest_services.NewRequest();
  guest_launch_info.flat_namespace = std::move(launch_info.flat_namespace);
  app_launcher_->CreateApplication(std::move(guest_launch_info),
                                   guest_app_controller.NewRequest());

  auto& label = launch_info.label ? launch_info.label : launch_info.url;
  auto holder = std::make_unique<GuestHolder>(g_next_guest_id++, label,
                                              std::move(guest_services),
                                              std::move(guest_app_controller));
  guest_app_controller.set_error_handler(
      [this, holder = holder.get()] { guests_.erase(holder); });
  holder->Bind(std::move(controller));
  guests_.insert({holder.get(), std::move(holder)});
}

void GuestEnvironmentImpl::GetSocketEndpoint(
    fidl::InterfaceRequest<guest::SocketEndpoint> request) {
  // TODO(tjdetwiler): Implement vsock APIs.
}

void GuestEnvironmentImpl::CreateApplicationEnvironment(
    const std::string& label) {
  context_->environment()->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), label);
  env_->GetApplicationLauncher(app_launcher_.NewRequest());
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0) {
    return;
  }
  context_->environment()->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));
}

}  // namespace guestmgr
