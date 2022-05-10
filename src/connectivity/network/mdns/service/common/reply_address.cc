// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/common/reply_address.h"

namespace mdns {

ReplyAddress::ReplyAddress()
    : socket_address_(inet::SocketAddress::kInvalid),
      interface_address_(inet::IpAddress::kInvalid),
      interface_id_(0),
      media_(Media::kBoth),
      ip_versions_(IpVersions::kBoth) {}

ReplyAddress::ReplyAddress(const inet::SocketAddress& socket_address,
                           const inet::IpAddress& interface_address, uint32_t interface_id,
                           Media media, IpVersions ip_versions)
    : socket_address_(socket_address),
      interface_address_(interface_address),
      interface_id_(interface_id),
      media_(media),
      ip_versions_(ip_versions) {}

ReplyAddress::ReplyAddress(const sockaddr_storage& socket_address,
                           const inet::IpAddress& interface_address, uint32_t interface_id,
                           Media media, IpVersions ip_versions)
    : socket_address_(socket_address),
      interface_address_(interface_address),
      interface_id_(interface_id),
      media_(media),
      ip_versions_(ip_versions) {}

}  // namespace mdns
