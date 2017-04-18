// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "apps/netconnector/src/socket_address.h"

namespace netconnector {
namespace mdns {

struct MdnsAddresses {
  static const SocketAddress kV4Multicast;
  static const SocketAddress kV6Multicast;
  static const SocketAddress kV4Bind;
  static const SocketAddress kV6Bind;
};

}  // namespace mdns
}  // namespace netconnector
