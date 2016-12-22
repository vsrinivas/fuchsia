// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/requestor_agent.h"

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "apps/netconnector/src/netconnector_impl.h"
#include "lib/ftl/logging.h"

namespace netconnector {

// static
std::unique_ptr<RequestorAgent> RequestorAgent::Create(
    const std::string& address,
    uint16_t port,
    const std::string& service_name,
    mx::channel local_channel,
    NetConnectorImpl* owner) {
  FTL_DCHECK(!address.empty());
  FTL_DCHECK(!service_name.empty());
  FTL_DCHECK(local_channel);
  FTL_DCHECK(owner != nullptr);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  struct addrinfo* addrinfos;
  int result = getaddrinfo(address.c_str(), nullptr, &hints, &addrinfos);
  if (result != 0) {
    FTL_LOG(ERROR) << "Failed to getaddrinfo for address " << address
                   << ", errno" << errno;
    return std::unique_ptr<RequestorAgent>();
  }

  for (struct addrinfo* addrinfo = addrinfos; addrinfo != nullptr;
       addrinfo = addrinfo->ai_next) {
    if (addrinfo->ai_family == AF_INET || addrinfo->ai_family == AF_INET6) {
      ftl::UniqueFD fd(socket(addrinfo->ai_family, addrinfo->ai_socktype,
                              addrinfo->ai_protocol));
      if (!fd.is_valid()) {
        FTL_LOG(WARNING) << "Failed to open requestor agent socket, errno"
                         << errno;
        continue;
      }

      SetPort(addrinfo->ai_addr, port);

      if (connect(fd.get(), addrinfo->ai_addr, addrinfo->ai_addrlen) < 0) {
        FTL_LOG(WARNING) << "Failed to connect, errno" << errno;
        continue;
      }

      return std::unique_ptr<RequestorAgent>(new RequestorAgent(
          std::move(fd), service_name, std::move(local_channel), owner));
    } else {
      FTL_LOG(WARNING) << "Unrecognized address family " << addrinfo->ai_family;
      continue;
    }
  }

  FTL_LOG(ERROR) << "Failed to connect to requestor_agent at " << address;
  return std::unique_ptr<RequestorAgent>();
}

// static
void RequestorAgent::SetPort(struct sockaddr* addr, uint16_t port) {
  switch (addr->sa_family) {
    case AF_INET: {
      struct sockaddr_in* addr_in = reinterpret_cast<struct sockaddr_in*>(addr);
      addr_in->sin_port = htons(port);
      break;
    }
    case AF_INET6: {
      struct sockaddr_in6* addr_in6 =
          reinterpret_cast<struct sockaddr_in6*>(addr);
      addr_in6->sin6_port = htons(port);
      break;
    }
    default:
      FTL_CHECK(false) << "Unrecognized address family " << addr->sa_family;
  }
}

RequestorAgent::RequestorAgent(ftl::UniqueFD socket_fd,
                               const std::string& service_name,
                               mx::channel local_channel,
                               NetConnectorImpl* owner)
    : MessageTransciever(std::move(socket_fd)),
      service_name_(service_name),
      local_channel_(std::move(local_channel)),
      owner_(owner) {
  FTL_DCHECK(!service_name_.empty());
  FTL_DCHECK(local_channel_);
  FTL_DCHECK(owner_ != nullptr);
}

RequestorAgent::~RequestorAgent() {}

void RequestorAgent::OnVersionReceived(uint32_t version) {
  SendServiceName(service_name_);
  SetChannel(std::move(local_channel_));
}

void RequestorAgent::OnServiceNameReceived(const std::string& service_name) {
  FTL_LOG(ERROR) << "RequestorAgent received service name";
  CloseConnection();
}

void RequestorAgent::OnConnectionClosed() {
  FTL_DCHECK(owner_ != nullptr);
  owner_->ReleaseRequestorAgent(this);
}

}  // namespace netconnector
