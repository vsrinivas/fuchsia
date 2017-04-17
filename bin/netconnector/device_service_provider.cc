// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/device_service_provider.h"

#include <memory>

#include "apps/netconnector/src/netconnector_impl.h"
#include "apps/netconnector/src/requestor_agent.h"
#include "apps/netconnector/src/socket_address.h"
#include "lib/ftl/logging.h"

namespace netconnector {

// static
std::unique_ptr<DeviceServiceProvider> DeviceServiceProvider::Create(
    const std::string& device_name,
    const SocketAddress& address,
    fidl::InterfaceRequest<app::ServiceProvider> request,
    NetConnectorImpl* owner) {
  return std::unique_ptr<DeviceServiceProvider>(new DeviceServiceProvider(
      device_name, address, std::move(request), owner));
}

DeviceServiceProvider::DeviceServiceProvider(
    const std::string& device_name,
    const SocketAddress& address,
    fidl::InterfaceRequest<app::ServiceProvider> request,
    NetConnectorImpl* owner)
    : device_name_(device_name),
      address_(address),
      binding_(this, std::move(request)),
      owner_(owner) {
  FTL_DCHECK(!device_name_.empty());
  FTL_DCHECK(address_.is_valid());
  FTL_DCHECK(binding_.is_bound());
  FTL_DCHECK(owner_ != nullptr);

  binding_.set_connection_error_handler([this]() {
    FTL_DCHECK(owner_ != nullptr);
    owner_->ReleaseDeviceServiceProvider(this);
  });
}

DeviceServiceProvider::~DeviceServiceProvider() {}

void DeviceServiceProvider::ConnectToService(const fidl::String& service_name,
                                             mx::channel channel) {
  std::unique_ptr<RequestorAgent> requestor_agent = RequestorAgent::Create(
      address_, service_name, std::move(channel), owner_);

  if (!requestor_agent) {
    FTL_LOG(ERROR) << "Connection failed, device " << device_name_;
    return;
  }

  owner_->AddRequestorAgent(std::move(requestor_agent));
}

}  // namespace netconnector
