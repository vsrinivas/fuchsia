// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/reply_address.h"

#include <sstream>

namespace mdns {

ReplyAddress::ReplyAddress()
    : socket_address_(inet::SocketAddress::kInvalid),
      interface_address_(inet::IpAddress::kInvalid),
      media_(Media::kBoth) {}

ReplyAddress::ReplyAddress(const inet::SocketAddress& socket_address,
                           const inet::IpAddress& interface_address, Media media)
    : socket_address_(socket_address), interface_address_(interface_address), media_(media) {}

ReplyAddress::ReplyAddress(const sockaddr_storage& socket_address,
                           const inet::IpAddress& interface_address, Media media)
    : socket_address_(socket_address), interface_address_(interface_address), media_(media) {}

std::ostream& operator<<(std::ostream& os, const Media& value) {
  return os << MediaStrings.at(static_cast<size_t>(value));
}

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value) {
  if (!value.socket_address().is_valid()) {
    return os << "<invalid>";
  }

  return os << value.socket_address() << " interface " << value.interface_address() << " media " << value.media();
}

}  // namespace mdns
