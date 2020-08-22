// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_ADDRESSES_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_ADDRESSES_H_

#include "src/connectivity/network/mdns/service/reply_address.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

class MdnsAddresses {
 public:
  // Sets the port to use. The default is 5353.
  void SetPort(inet::IpPort port);

  // Sets the V4 or V6 multicast IP address to use. The default for V4 is
  // 244.0.0.251. The default for V6 is ff02::fb.
  void SetMulticastAddress(inet::IpAddress address);

  // Gets the mDNS port.
  inet::IpPort port() const { return port_; }

  // Gets the V4 multicast socket address.
  inet::SocketAddress v4_multicast() const { return inet::SocketAddress(v4_multicast_, port_); }

  // Gets the V6 multicast socket address.
  inet::SocketAddress v6_multicast() const { return inet::SocketAddress(v6_multicast_, port_); }

  // Gets the V4 socket address to bind to.
  inet::SocketAddress v4_bind() const { return inet::SocketAddress(INADDR_ANY, port_); }

  // Gets the V6 socket address to bind to.
  inet::SocketAddress v6_bind() const { return inet::SocketAddress(in6addr_any, port_); }

  // Gets the placeholder multicast reply address. This address is used when sending messages and
  // represents the appropriate reply address based on context.
  ReplyAddress multicast_reply() const {
    return ReplyAddress(v4_multicast(), inet::IpAddress(), Media::kBoth);
  }

  // Gets the placeholder multicast reply address for wired interfaces only. This address is used
  // when sending messages and represents the appropriate reply address based on context.
  ReplyAddress multicast_reply_wired_only() const {
    return ReplyAddress(v4_multicast(), inet::IpAddress(), Media::kWired);
  }

  // Gets the placeholder multicast reply address for wireless interfaces only. This address is used
  // when sending messages and represents the appropriate reply address based on context.
  ReplyAddress multicast_reply_wireless_only() const {
    return ReplyAddress(v4_multicast(), inet::IpAddress(), Media::kWireless);
  }

 private:
  static const inet::IpPort kDefaultMdnsPort;
  static const inet::IpAddress kDefaultV4MulticastAddress;
  static const inet::IpAddress kDefaultV6MulticastAddress;

  inet::IpPort port_ = kDefaultMdnsPort;
  inet::IpAddress v4_multicast_ = kDefaultV4MulticastAddress;
  inet::IpAddress v6_multicast_ = kDefaultV6MulticastAddress;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_ADDRESSES_H_
