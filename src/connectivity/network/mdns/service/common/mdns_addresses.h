// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_MDNS_ADDRESSES_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_MDNS_ADDRESSES_H_

#include "src/lib/inet/socket_address.h"

namespace mdns {

class MdnsAddresses {
 public:
  // Gets the mDNS port.
  static inet::IpPort port() { return kMdnsPort; }

  // Gets the V4 multicast socket address.
  static inet::SocketAddress v4_multicast() {
    return inet::SocketAddress(kV4MulticastAddress, kMdnsPort);
  }

  // Gets the V6 multicast socket address.
  static inet::SocketAddress v6_multicast() {
    return inet::SocketAddress(kV6MulticastAddress, kMdnsPort);
  }

  // Gets the V4 socket address to bind to.
  static inet::SocketAddress v4_bind() { return inet::SocketAddress(INADDR_ANY, kMdnsPort); }

  // Gets the V6 socket address to bind to.
  static inet::SocketAddress v6_bind() { return inet::SocketAddress(in6addr_any, kMdnsPort); }

 private:
  static const inet::IpPort kMdnsPort;
  static const inet::IpAddress kV4MulticastAddress;
  static const inet::IpAddress kV6MulticastAddress;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_COMMON_MDNS_ADDRESSES_H_
