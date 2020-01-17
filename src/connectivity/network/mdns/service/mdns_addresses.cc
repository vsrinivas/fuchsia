// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns_addresses.h"

#include "src/lib/syslog/cpp/logger.h"

namespace mdns {

void MdnsAddresses::SetPort(inet::IpPort port) { port_ = port; }

void MdnsAddresses::SetMulticastAddress(inet::IpAddress address) {
  FX_DCHECK(address.is_valid());
  if (address.is_v4()) {
    v4_multicast_ = address;
  } else {
    FX_DCHECK(address.is_v6());
    v6_multicast_ = address;
  }
}

// static
const inet::IpPort MdnsAddresses::kDefaultMdnsPort = inet::IpPort::From_uint16_t(5353);

// static
const inet::IpAddress MdnsAddresses::kDefaultV4MulticastAddress(224, 0, 0, 251);

// static
const inet::IpAddress MdnsAddresses::kDefaultV6MulticastAddress(0xff02, 0xfb);

}  // namespace mdns
