// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_interface_transceiver_v4.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "garnet/bin/mdns/service/mdns_addresses.h"
#include "lib/fxl/logging.h"

namespace mdns {

MdnsInterfaceTransceiverV4::MdnsInterfaceTransceiverV4(IpAddress address,
                                                       const std::string& name,
                                                       uint32_t index)
    : MdnsInterfaceTransceiver(address, name, index) {}

MdnsInterfaceTransceiverV4::~MdnsInterfaceTransceiverV4() {}

int MdnsInterfaceTransceiverV4::SetOptionJoinMulticastGroup() {
  ip_mreqn param;
  param.imr_multiaddr.s_addr =
      MdnsAddresses::kV4Multicast.as_sockaddr_in().sin_addr.s_addr;
  param.imr_address = address().as_in_addr();
  param.imr_ifindex = 0;
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_ADD_MEMBERSHIP,
                          &param, sizeof(param));
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to set socket option IP_ADD_MEMBERSHIP, errno "
                   << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionOutboundInterface() {
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_MULTICAST_IF,
                          &address().as_in_addr(), sizeof(struct in_addr));
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to set socket option IP_MULTICAST_IF, errno "
                   << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionUnicastTtl() {
  int param = kTimeToLive_;
  int result =
      setsockopt(socket_fd().get(), IPPROTO_IP, IP_TTL, &param, sizeof(param));
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to set socket option IP_TTL, errno " << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionMulticastTtl() {
  uint8_t param = kTimeToLive_;
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_MULTICAST_TTL,
                          &param, sizeof(param));
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to set socket option IP_MULTICAST_TTL, errno "
                   << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionFamilySpecific() {
  // Nothing to do.
  return 0;
}

int MdnsInterfaceTransceiverV4::Bind() {
  int result = bind(socket_fd().get(), MdnsAddresses::kV4Bind.as_sockaddr(),
                    MdnsAddresses::kV4Bind.socklen());
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to bind socket to V4 address, errno " << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SendTo(const void* buffer, size_t size,
                                       const SocketAddress& address) {
  return sendto(socket_fd().get(), buffer, size, 0, address.as_sockaddr(),
                address.socklen());
}

}  // namespace mdns
