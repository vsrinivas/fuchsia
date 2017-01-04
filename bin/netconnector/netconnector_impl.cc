// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/netconnector_impl.h"

#include "apps/netconnector/src/device_service_provider.h"
#include "apps/netconnector/src/netconnector_params.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace netconnector {

NetConnectorImpl::NetConnectorImpl(NetConnectorParams* params)
    : params_(params),
      application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()),
      // TODO(dalesat): Create a new RespondingServiceHost per user.
      // Requestors should provide user credentials allowing a ServiceAgent to
      // obtain a user environment. A RespondingServiceHost should be created
      // with that environment so that responding services are launched in the
      // correct environment.
      responding_service_host_(application_context_->environment()) {
  if (!params->listen()) {
    // Start the listener.
    FTL_DCHECK(!params->listen());

    netconnector::NetConnectorAdminPtr net_connector_admin =
        application_context_
            ->ConnectToEnvironmentService<netconnector::NetConnectorAdmin>();

    if (!params->host_name().empty()) {
      net_connector_admin->SetHostName(params->host_name());
    }

    for (auto& pair : params_->MoveServices()) {
      net_connector_admin->RegisterService(pair.first, std::move(pair.second));
    }

    for (auto& pair : params_->devices()) {
      net_connector_admin->RegisterDevice(pair.first, pair.second);
    }

    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  // Running as the listener.

  if (!params->host_name().empty()) {
    SetHostName(params->host_name());
  }

  // Register services.
  for (auto& pair : params->MoveServices()) {
    responding_service_host_.RegisterSingleton(pair.first,
                                               std::move(pair.second));
  }

  listener_.Start(kPort, [this](ftl::UniqueFD fd) {
    AddServiceAgent(ServiceAgent::Create(std::move(fd), this));
  });

  application_context_->outgoing_services()->AddService<NetConnector>(
      [this](fidl::InterfaceRequest<NetConnector> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  application_context_->outgoing_services()->AddService<NetConnectorAdmin>(
      [this](fidl::InterfaceRequest<NetConnectorAdmin> request) {
        admin_bindings_.AddBinding(this, std::move(request));
      });
}

NetConnectorImpl::~NetConnectorImpl() {}

void NetConnectorImpl::ReleaseDeviceServiceProvider(
    DeviceServiceProvider* device_service_provider) {
  size_t removed = device_service_providers_.erase(device_service_provider);
  FTL_DCHECK(removed == 1);
}

void NetConnectorImpl::ReleaseRequestorAgent(RequestorAgent* requestor_agent) {
  size_t removed = requestor_agents_.erase(requestor_agent);
  FTL_DCHECK(removed == 1);
}

void NetConnectorImpl::ReleaseServiceAgent(ServiceAgent* service_agent) {
  size_t removed = service_agents_.erase(service_agent);
  FTL_DCHECK(removed == 1);
}

void NetConnectorImpl::GetDeviceServiceProvider(
    const fidl::String& device_name,
    fidl::InterfaceRequest<modular::ServiceProvider> request) {
  auto iter = params_->devices().find(device_name);
  if (iter == params_->devices().end()) {
    FTL_LOG(ERROR) << "Unrecognized device name " << device_name;
    return;
  }

  AddDeviceServiceProvider(DeviceServiceProvider::Create(
      device_name, iter->second, kPort, std::move(request), this));
}

void NetConnectorImpl::SetHostName(const fidl::String& host_name) {
  FTL_LOG(INFO) << "Host name set to '" << host_name << "'";
  // We have no use for the host name until mDNS is implemented.
  // TODO(dalesat): Implement mDNS.
}

void NetConnectorImpl::RegisterService(
    const fidl::String& name,
    modular::ApplicationLaunchInfoPtr launch_info) {
  FTL_LOG(INFO) << "Service '" << name << "' registered.";
  responding_service_host_.RegisterSingleton(name, std::move(launch_info));
}

void NetConnectorImpl::RegisterDevice(const fidl::String& name,
                                      const fidl::String& address) {
  FTL_LOG(INFO) << "Device '" << name << "' registered at address " << address;
  params_->RegisterDevice(name, address);
}

void NetConnectorImpl::RegisterServiceProvider(
    const fidl::String& name,
    fidl::InterfaceHandle<modular::ServiceProvider> handle) {
  FTL_LOG(INFO) << "Service '" << name << "' provider registered.";
  responding_service_host_.RegisterProvider(name, std::move(handle));
}

void NetConnectorImpl::AddDeviceServiceProvider(
    std::unique_ptr<DeviceServiceProvider> device_service_provider) {
  DeviceServiceProvider* raw_ptr = device_service_provider.get();
  device_service_providers_.emplace(raw_ptr,
                                    std::move(device_service_provider));
}

void NetConnectorImpl::AddRequestorAgent(
    std::unique_ptr<RequestorAgent> requestor_agent) {
  RequestorAgent* raw_ptr = requestor_agent.get();
  requestor_agents_.emplace(raw_ptr, std::move(requestor_agent));
}

void NetConnectorImpl::AddServiceAgent(
    std::unique_ptr<ServiceAgent> service_agent) {
  ServiceAgent* raw_ptr = service_agent.get();
  service_agents_.emplace(raw_ptr, std::move(service_agent));
}

}  // namespace netconnector
