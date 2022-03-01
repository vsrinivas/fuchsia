// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"

#include <lib/syslog/cpp/macros.h>

namespace mdns {

// static
const inet::IpPort MdnsAddresses::kMdnsPort = inet::IpPort::From_uint16_t(5353);

// static
const inet::IpAddress MdnsAddresses::kV4MulticastAddress(224, 0, 0, 251);

// static
const inet::IpAddress MdnsAddresses::kV6MulticastAddress(0xff02, 0xfb);

}  // namespace mdns
