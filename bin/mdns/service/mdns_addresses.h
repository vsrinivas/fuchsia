// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_MDNS_ADDRESSES_H_
#define GARNET_BIN_MDNS_SERVICE_MDNS_ADDRESSES_H_

#include <memory>
#include <vector>

#include "garnet/bin/mdns/service/reply_address.h"
#include "garnet/bin/mdns/service/socket_address.h"

namespace mdns {

struct MdnsAddresses {
  static const IpPort kMdnsPort;

  static const SocketAddress kV4Multicast;
  static const SocketAddress kV6Multicast;
  static const SocketAddress kV4Bind;
  static const SocketAddress kV6Bind;

  static const ReplyAddress kV4MulticastReply;
  static const ReplyAddress kV6MulticastReply;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_MDNS_ADDRESSES_H_
