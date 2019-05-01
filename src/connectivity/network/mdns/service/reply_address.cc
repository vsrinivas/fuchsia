// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/reply_address.h"

#include <sstream>

namespace mdns {

ReplyAddress::ReplyAddress(const inet::SocketAddress& socket_address,
                           const inet::IpAddress& interface_address)
    : socket_address_(socket_address), interface_address_(interface_address) {}

ReplyAddress::ReplyAddress(const sockaddr_storage& socket_address,
                           const inet::IpAddress& interface_address)
    : socket_address_(socket_address), interface_address_(interface_address) {}

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value) {
  if (!value.socket_address().is_valid()) {
    return os << "<invalid>";
  }

  return os << value.socket_address() << " interface "
            << value.interface_address();
}

}  // namespace mdns
