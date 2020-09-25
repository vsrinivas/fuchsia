// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns_interface_transceiver_v4.h"

#include <arpa/inet.h>
#include <errno.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/socket.h>

#include "src/connectivity/network/mdns/service/mdns_addresses.h"

namespace mdns {

MdnsInterfaceTransceiverV4::MdnsInterfaceTransceiverV4(inet::IpAddress address,
                                                       const std::string& name, uint32_t index,
                                                       Media media)
    : MdnsInterfaceTransceiver(address, name, index, media) {}

MdnsInterfaceTransceiverV4::~MdnsInterfaceTransceiverV4() {}

int MdnsInterfaceTransceiverV4::SetOptionDisableMulticastLoop() {
  char param = 0;
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_MULTICAST_LOOP, &param, sizeof(param));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option IP_MULTICAST_LOOP, " << strerror(errno);
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionJoinMulticastGroup() {
  ip_mreqn param;
  param.imr_multiaddr.s_addr = addresses().v4_multicast().as_sockaddr_in().sin_addr.s_addr;
  param.imr_address = address().as_in_addr();
  param.imr_ifindex = index();
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option IP_ADD_MEMBERSHIP, " << strerror(errno);
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionOutboundInterface() {
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_MULTICAST_IF, &address().as_in_addr(),
                          sizeof(struct in_addr));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option IP_MULTICAST_IF, " << strerror(errno);
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionUnicastTtl() {
  int param = kTimeToLive_;
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_TTL, &param, sizeof(param));
  if (result < 0) {
    if (errno == ENOPROTOOPT) {
      FX_LOGS(WARNING) << "fxbug.dev/21170 IP_TTL not supported (ENOPROTOOPT), "
                          "continuing anyway. May cause spurious IP traffic";
      result = 0;
    } else {
      FX_LOGS(ERROR) << "Failed to set socket option IP_TTL, " << strerror(errno);
    }
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionMulticastTtl() {
  int param = kTimeToLive_;
  int result = setsockopt(socket_fd().get(), IPPROTO_IP, IP_MULTICAST_TTL, &param, sizeof(param));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option IP_MULTICAST_TTL, " << strerror(errno);
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SetOptionFamilySpecific() {
  // Nothing to do.
  return 0;
}

int MdnsInterfaceTransceiverV4::Bind() {
  int result =
      bind(socket_fd().get(), addresses().v4_bind().as_sockaddr(), addresses().v4_bind().socklen());
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to bind socket to V4 address, " << strerror(errno);
  }

  return result;
}

int MdnsInterfaceTransceiverV4::SendTo(const void* buffer, size_t size,
                                       const inet::SocketAddress& address) {
  return sendto(socket_fd().get(), buffer, size, 0, address.as_sockaddr(), address.socklen());
}

}  // namespace mdns
