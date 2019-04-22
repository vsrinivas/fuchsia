// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_addresses.h"

namespace mdns {

// static
const inet::IpPort MdnsAddresses::kDefaultMdnsPort =
    inet::IpPort::From_uint16_t(5353);

// static
inet::SocketAddress MdnsAddresses::V4Multicast(inet::IpPort port) {
  return inet::SocketAddress(224, 0, 0, 251, port);
}

// static
inet::SocketAddress MdnsAddresses::V6Multicast(inet::IpPort port) {
  return inet::SocketAddress(0xff02, 0xfb, port);
}

// static
inet::SocketAddress MdnsAddresses::V4Bind(inet::IpPort port) {
  return inet::SocketAddress(INADDR_ANY, port);
}

// static
inet::SocketAddress MdnsAddresses::V6Bind(inet::IpPort port) {
  return inet::SocketAddress(in6addr_any, port);
}

// static
ReplyAddress MdnsAddresses::V4MulticastReply(inet::IpPort port) {
  return ReplyAddress(V4Multicast(port), inet::IpAddress());
}

}  // namespace mdns
