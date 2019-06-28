// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shared constants between different components of the packet filtering feature.

#ifndef ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_CONSTANTS_H_
#define ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_CONSTANTS_H_

extern "C" {
#include <inet6/inet6.h>
}
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

namespace netdump {

// Specifies whether matching should occur on the src or dst fields.
// TODO(xianglong): Extend to e.g. receiver, transmitter types when there is WLAN support.
enum AddressFieldType {
  SRC_ADDR = 0b01,
  DST_ADDR = 0b10,
  EITHER_ADDR = SRC_ADDR | DST_ADDR,
};

enum PortFieldType {
  SRC_PORT = 0b01,
  DST_PORT = 0b10,
  EITHER_PORT = SRC_PORT | DST_PORT,
};

enum LengthComparator {
  LEQ,
  GEQ,
};

constexpr uint16_t ETH_P_IP_NETWORK_BYTE_ORDER = 0x0008;
constexpr uint16_t ETH_P_IPV6_NETWORK_BYTE_ORDER = 0xDD86;

// Port ranges are specified as pairs of (begin, end) port numbers.
using PortRange = std::pair<uint16_t, uint16_t>;

}  // namespace netdump

#endif  // ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_CONSTANTS_H_
