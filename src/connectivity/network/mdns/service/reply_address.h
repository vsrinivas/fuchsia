// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_REPLY_ADDRESS_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_REPLY_ADDRESS_H_

#include <arpa/inet.h>
#include <sys/socket.h>

#include <ostream>

#include "src/lib/inet/socket_address.h"

namespace mdns {

enum class Media { kWired, kWireless, kBoth };
const std::array<std::string, 3> MediaStrings = { "Wired", "Wireless", "Both" };

std::ostream& operator<<(std::ostream& os, const Media& value);

// SocketAddress with interface address.
class ReplyAddress {
 public:
  // Creates a reply address with an invalid socket address and interface.
  ReplyAddress();

  // Creates a reply address from an |SocketAddress| and an interface |IpAddress|.
  ReplyAddress(const inet::SocketAddress& socket_address, const inet::IpAddress& interface_address,
               Media media);

  // Creates a reply address from an |sockaddr_storage| struct and an interface
  // |IpAddress|.
  ReplyAddress(const sockaddr_storage& socket_address, const inet::IpAddress& interface_address,
               Media media);

  const inet::SocketAddress& socket_address() const { return socket_address_; }

  const inet::IpAddress& interface_address() const { return interface_address_; }

  // For unicast reply addresses, this field is set to |kWired| or |kWireless| to describe the
  // interface. For multicast reply addresses, this field is set to |kWired| to multicast via
  // wired interfaces only, |kWireless| to multicast via wireless interfaces only, or |kBoth|
  // to multicast via all interfaces.
  Media media() const { return media_; }

  bool operator==(const ReplyAddress& other) const {
    return socket_address_ == other.socket_address() &&
           interface_address_ == other.interface_address() && media_ == other.media_;
  }

  bool operator!=(const ReplyAddress& other) const { return !(*this == other); }

 private:
  inet::SocketAddress socket_address_;
  inet::IpAddress interface_address_;
  Media media_;
};

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value);

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_REPLY_ADDRESS_H_
