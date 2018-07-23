// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/guest_environment_impl.h"

#include <lib/fxl/logging.h>

namespace guestmgr {

GuestEnvironmentImpl::GuestEnvironmentImpl(
    uint32_t id, const std::string& label, component::StartupContext* context,
    fidl::InterfaceRequest<fuchsia::guest::GuestEnvironment> request)
    : id_(id),
      label_(label),
      context_(context),
      host_vsock_endpoint_(
          fit::bind_member(this, &GuestEnvironmentImpl::GetAcceptor)) {
  // Create environment.
  context_->environment()->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), label);
  env_->GetLauncher(launcher_.NewRequest());
  zx::channel h1, h2;
  FXL_CHECK(zx::channel::create(0, &h1, &h2) == ZX_OK);
  context_->environment()->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));

  AddBinding(std::move(request));
}

void GuestEnvironmentImpl::set_unbound_handler(std::function<void()> handler) {
  bindings_.set_empty_set_handler(std::move(handler));
}

void GuestEnvironmentImpl::AddBinding(
    fidl::InterfaceRequest<GuestEnvironment> request) {
  bindings_.AddBinding(this, std::move(request));
}

void GuestEnvironmentImpl::LaunchGuest(
    fuchsia::guest::GuestLaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::guest::GuestController> controller,
    LaunchGuestCallback callback) {
  component::Services services;
  fuchsia::sys::ComponentControllerPtr component_controller;
  fuchsia::sys::LaunchInfo info;
  info.url = launch_info.url;
  info.arguments = std::move(launch_info.vmm_args);
  info.directory_request = services.NewRequest();
  info.flat_namespace = std::move(launch_info.flat_namespace);
  launcher_->CreateComponent(std::move(info),
                             component_controller.NewRequest());

  // Setup guest endpoint.
  uint32_t cid = next_guest_cid_++;
  fuchsia::guest::GuestVsockEndpointPtr guest_endpoint;
  services.ConnectToService(guest_endpoint.NewRequest());
  auto endpoint = std::make_unique<GuestVsockEndpoint>(
      cid, std::move(guest_endpoint), &host_vsock_endpoint_);

  auto& label = launch_info.label ? launch_info.label : launch_info.url;
  component_controller.set_error_handler([this, cid] { guests_.erase(cid); });
  auto component = std::make_unique<GuestComponent>(
      label, std::move(endpoint), std::move(services),
      std::move(component_controller));
  component->AddBinding(std::move(controller));

  bool inserted;
  std::tie(std::ignore, inserted) = guests_.insert({cid, std::move(component)});
  if (!inserted) {
    FXL_LOG(ERROR) << "Failed to allocate guest endpoint on CID " << cid;
    callback(fuchsia::guest::GuestInfo());
    return;
  }

  fuchsia::guest::GuestInfo guest_info;
  guest_info.cid = cid;
  guest_info.label = label;
  callback(std::move(guest_info));
}

void GuestEnvironmentImpl::GetHostVsockEndpoint(
    fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> request) {
  host_vsock_endpoint_.AddBinding(std::move(request));
}

fidl::VectorPtr<fuchsia::guest::GuestInfo> GuestEnvironmentImpl::ListGuests() {
  fidl::VectorPtr<fuchsia::guest::GuestInfo> guest_infos =
      fidl::VectorPtr<fuchsia::guest::GuestInfo>::New(0);
  for (const auto& it : guests_) {
    fuchsia::guest::GuestInfo guest_info;
    guest_info.cid = it.first;
    guest_info.label = it.second->label();
    guest_infos.push_back(std::move(guest_info));
  }
  return guest_infos;
}

void GuestEnvironmentImpl::ListGuests(ListGuestsCallback callback) {
  callback(ListGuests());
}

void GuestEnvironmentImpl::ConnectToGuest(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::guest::GuestController> request) {
  const auto& it = guests_.find(id);
  if (it != guests_.end()) {
    it->second->AddBinding(std::move(request));
  }
}

fuchsia::guest::GuestVsockAcceptor* GuestEnvironmentImpl::GetAcceptor(
    uint32_t cid) {
  const auto& it = guests_.find(cid);
  return it == guests_.end() ? nullptr : it->second->endpoint();
}

}  // namespace guestmgr
