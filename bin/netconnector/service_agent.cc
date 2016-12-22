// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/service_agent.h"

#include "apps/netconnector/src/netconnector_impl.h"
#include "lib/ftl/logging.h"

namespace netconnector {

// static
std::unique_ptr<ServiceAgent> ServiceAgent::Create(ftl::UniqueFD socket_fd,
                                                   NetConnectorImpl* owner) {
  return std::unique_ptr<ServiceAgent>(
      new ServiceAgent(std::move(socket_fd), owner));
}

ServiceAgent::ServiceAgent(ftl::UniqueFD socket_fd, NetConnectorImpl* owner)
    : MessageTransciever(std::move(socket_fd)), owner_(owner) {
  FTL_DCHECK(owner != nullptr);
}

ServiceAgent::~ServiceAgent() {}

void ServiceAgent::OnVersionReceived(uint32_t version) {}

void ServiceAgent::OnServiceNameReceived(const std::string& service_name) {
  mx::channel local;
  mx::channel remote;
  mx_status_t status = mx::channel::create(0u, &local, &remote);

  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to create channel, status " << status;
    CloseConnection();
    return;
  }

  owner_->responding_services()->ConnectToService(service_name,
                                                  std::move(remote));

  SetChannel(std::move(local));
}

void ServiceAgent::OnConnectionClosed() {
  FTL_DCHECK(owner_ != nullptr);
  owner_->ReleaseServiceAgent(this);
}

}  // namespace netconnector
