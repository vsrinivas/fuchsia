// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_addresses.h"

#include "apps/netconnector/src/socket_address.h"

namespace netconnector {
namespace mdns {

// static
const SocketAddress MdnsAddresses::kV4Multicast(224,
                                                0,
                                                0,
                                                251,
                                                IpPort::From_uint16_t(5353));

// static
const SocketAddress MdnsAddresses::kV6Multicast(0xff02,
                                                0xfb,
                                                IpPort::From_uint16_t(5353));

// static
const SocketAddress MdnsAddresses::kV4Bind(INADDR_ANY,
                                           IpPort::From_uint16_t(5353));

// static
const SocketAddress MdnsAddresses::kV6Bind(in6addr_any,
                                           IpPort::From_uint16_t(5353));

}  // namespace mdns
}  // namespace netconnector
