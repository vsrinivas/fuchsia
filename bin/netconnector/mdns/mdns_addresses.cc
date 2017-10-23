// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/mdns_addresses.h"

#include "garnet/bin/netconnector/socket_address.h"

namespace netconnector {
namespace mdns {

// static
const IpPort MdnsAddresses::kMdnsPort = IpPort::From_uint16_t(5353);

// static
const SocketAddress MdnsAddresses::kV4Multicast(224, 0, 0, 251, kMdnsPort);

// static
const SocketAddress MdnsAddresses::kV6Multicast(0xff02, 0xfb, kMdnsPort);

// static
const SocketAddress MdnsAddresses::kV4Bind(INADDR_ANY, kMdnsPort);

// static
const SocketAddress MdnsAddresses::kV6Bind(in6addr_any, kMdnsPort);

// static
const ReplyAddress MdnsAddresses::kV4MulticastReply(MdnsAddresses::kV4Multicast,
                                                    0);

// static
const ReplyAddress MdnsAddresses::kV6MulticastReply(MdnsAddresses::kV6Multicast,
                                                    0);
}  // namespace mdns
}  // namespace netconnector
