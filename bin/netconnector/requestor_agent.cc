// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/requestor_agent.h"

#include <errno.h>
#include <sys/socket.h>

#include "garnet/bin/netconnector/ip_port.h"
#include "garnet/bin/netconnector/netconnector_impl.h"
#include "lib/fxl/logging.h"

namespace netconnector {

// static
std::unique_ptr<RequestorAgent> RequestorAgent::Create(
    const SocketAddress& address,
    const std::string& service_name,
    zx::channel local_channel,
    NetConnectorImpl* owner) {
  FXL_DCHECK(address.is_valid());
  FXL_DCHECK(!service_name.empty());
  FXL_DCHECK(local_channel);
  FXL_DCHECK(owner != nullptr);

  fxl::UniqueFD fd(socket(address.family(), SOCK_STREAM, 0));
  if (!fd.is_valid()) {
    FXL_LOG(WARNING) << "Failed to open requestor agent socket, errno" << errno;
    return std::unique_ptr<RequestorAgent>();
  }

  if (connect(fd.get(), address.as_sockaddr(), address.socklen()) < 0) {
    FXL_LOG(WARNING) << "Failed to connect, errno" << errno;
    return std::unique_ptr<RequestorAgent>();
  }

  return std::unique_ptr<RequestorAgent>(new RequestorAgent(
      std::move(fd), service_name, std::move(local_channel), owner));
}

RequestorAgent::RequestorAgent(fxl::UniqueFD socket_fd,
                               const std::string& service_name,
                               zx::channel local_channel,
                               NetConnectorImpl* owner)
    : MessageTransceiver(std::move(socket_fd)),
      service_name_(service_name),
      local_channel_(std::move(local_channel)),
      owner_(owner) {
  FXL_DCHECK(!service_name_.empty());
  FXL_DCHECK(local_channel_);
  FXL_DCHECK(owner_ != nullptr);
}

RequestorAgent::~RequestorAgent() {}

void RequestorAgent::OnVersionReceived(uint32_t version) {
  SendServiceName(service_name_);
  SetChannel(std::move(local_channel_));
}

void RequestorAgent::OnServiceNameReceived(const std::string& service_name) {
  FXL_LOG(ERROR) << "RequestorAgent received service name";
  CloseConnection();
}

void RequestorAgent::OnConnectionClosed() {
  FXL_DCHECK(owner_ != nullptr);
  owner_->ReleaseRequestorAgent(this);
}

}  // namespace netconnector
