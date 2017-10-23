// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/reply_address.h"

#include <sstream>

namespace netconnector {
namespace mdns {

ReplyAddress::ReplyAddress(const SocketAddress& socket_address,
                           uint32_t interface_index)
    : socket_address_(socket_address), interface_index_(interface_index) {}

ReplyAddress::ReplyAddress(const sockaddr_storage& socket_address,
                           uint32_t interface_index)
    : socket_address_(socket_address), interface_index_(interface_index) {}

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value) {
  if (!value.socket_address().is_valid()) {
    return os << "<invalid>";
  }

  return os << value.socket_address() << " interface "
            << value.interface_index();
}

}  // namespace mdns
}  // namespace netconnector
