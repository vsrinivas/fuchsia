// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/service_agent.h"

#include "garnet/bin/netconnector/netconnector_impl.h"
#include "lib/fxl/logging.h"

namespace netconnector {

// static
std::unique_ptr<ServiceAgent> ServiceAgent::Create(fxl::UniqueFD socket_fd,
                                                   NetConnectorImpl* owner) {
  return std::unique_ptr<ServiceAgent>(
      new ServiceAgent(std::move(socket_fd), owner));
}

ServiceAgent::ServiceAgent(fxl::UniqueFD socket_fd, NetConnectorImpl* owner)
    : MessageTransceiver(std::move(socket_fd)), owner_(owner) {
  FXL_DCHECK(owner != nullptr);
}

ServiceAgent::~ServiceAgent() {}

void ServiceAgent::OnVersionReceived(uint32_t version) {}

void ServiceAgent::OnServiceNameReceived(const std::string& service_name) {
  zx::channel local;
  zx::channel remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel, status " << status;
    CloseConnection();
    return;
  }

  owner_->responding_services()->ConnectToService(service_name,
                                                  std::move(remote));

  SetChannel(std::move(local));
}

void ServiceAgent::OnConnectionClosed() {
  FXL_DCHECK(owner_ != nullptr);
  owner_->ReleaseServiceAgent(this);
}

}  // namespace netconnector
