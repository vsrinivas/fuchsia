// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_MDNS_ADDRESSES_H_
#define GARNET_BIN_MDNS_SERVICE_MDNS_ADDRESSES_H_

#include <memory>
#include <vector>

#include "garnet/bin/mdns/service/reply_address.h"
#include "garnet/lib/inet/socket_address.h"

namespace mdns {

struct MdnsAddresses {
  static const inet::IpPort kDefaultMdnsPort;

  static inet::SocketAddress V4Multicast(inet::IpPort port);
  static inet::SocketAddress V6Multicast(inet::IpPort port);
  static inet::SocketAddress V4Bind(inet::IpPort port);
  static inet::SocketAddress V6Bind(inet::IpPort port);

  static ReplyAddress V4MulticastReply(inet::IpPort port);
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_MDNS_ADDRESSES_H_
