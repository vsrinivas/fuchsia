// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_interface_transceiver_v6.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "apps/netconnector/src/mdns/mdns_addresses.h"
#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {

MdnsInterfaceTransceiverV6::MdnsInterfaceTransceiverV6(
    const netc_if_info_t& if_info,
    uint32_t index)
    : MdnsInterfaceTransceiver(if_info, index) {}

MdnsInterfaceTransceiverV6::~MdnsInterfaceTransceiverV6() {}

int MdnsInterfaceTransceiverV6::SetOptionJoinMulticastGroup() {
  ipv6_mreq param;
  param.ipv6mr_multiaddr =
      MdnsAddresses::kV6Multicast.as_sockaddr_in6().sin6_addr;
  param.ipv6mr_interface = index();
  int result = setsockopt(socket_fd().get(), IPPROTO_IPV6, IPV6_JOIN_GROUP,
                          &param, sizeof(param));
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to set socket option IPV6_JOIN_GROUP, errno "
                   << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV6::SetOptionOutboundInterface() {
  uint32_t index = this->index();
  int result = setsockopt(socket_fd().get(), IPPROTO_IPV6, IPV6_MULTICAST_IF,
                          &index, sizeof(index));
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to set socket option IP_MULTICAST_IF, errno "
                   << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV6::SetOptionUnicastTtl() {
  int param = kTimeToLive_;
  int result = setsockopt(socket_fd().get(), IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                          &param, sizeof(param));
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to set socket option IPV6_UNICAST_HOPS, errno "
                   << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV6::SetOptionMulticastTtl() {
  int param = kTimeToLive_;
  int result = setsockopt(socket_fd().get(), IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                          &param, sizeof(param));
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to set socket option IPV6_MULTICAST_HOPS, errno "
                   << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV6::SetOptionFamilySpecific() {
  // Set hop limit.
  int param = 1;
  int result = setsockopt(socket_fd().get(), IPPROTO_IPV6, IPV6_HOPLIMIT,
                          &param, sizeof(param));
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to set socket option IPV6_HOPLIMIT, errno "
                   << errno;
    return result;
  }

  // Receive V6 packets only.
  param = 1;
  result = setsockopt(socket_fd().get(), IPPROTO_IPV6, IPV6_V6ONLY, &param,
                      sizeof(param));
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to set socket option IPV6_V6ONLY, errno "
                   << errno;
    return false;
  }

  return result;
}

int MdnsInterfaceTransceiverV6::Bind() {
  int result = bind(socket_fd().get(), MdnsAddresses::kV6Bind.as_sockaddr(),
                    MdnsAddresses::kV6Bind.socklen());
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to bind socket to V6 address, errno " << errno;
  }

  return result;
}

int MdnsInterfaceTransceiverV6::SendTo(const void* buffer,
                                       size_t size,
                                       const SocketAddress& address) {
  if (address == MdnsAddresses::kV4Multicast) {
    return sendto(socket_fd().get(), buffer, size, 0,
                  MdnsAddresses::kV6Multicast.as_sockaddr(),
                  MdnsAddresses::kV6Multicast.socklen());
  }

  return sendto(socket_fd().get(), buffer, size, 0, address.as_sockaddr(),
                address.socklen());
}

}  // namespace mdns
}  // namespace netconnector
