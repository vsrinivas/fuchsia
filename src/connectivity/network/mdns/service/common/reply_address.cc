// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/common/reply_address.h"

namespace mdns {

ReplyAddress::ReplyAddress()
    : socket_address_(inet::SocketAddress::kInvalid),
      interface_address_(inet::IpAddress::kInvalid),
      media_(Media::kBoth),
      ip_versions_(IpVersions::kBoth) {}

ReplyAddress::ReplyAddress(const inet::SocketAddress& socket_address,
                           const inet::IpAddress& interface_address, Media media,
                           IpVersions ip_versions)
    : socket_address_(socket_address),
      interface_address_(interface_address),
      media_(media),
      ip_versions_(ip_versions) {}

ReplyAddress::ReplyAddress(const sockaddr_storage& socket_address,
                           const inet::IpAddress& interface_address, Media media,
                           IpVersions ip_versions)
    : socket_address_(socket_address),
      interface_address_(interface_address),
      media_(media),
      ip_versions_(ip_versions) {}

}  // namespace mdns
