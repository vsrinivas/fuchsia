// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ostream>

#include <arpa/inet.h>
#include <sys/socket.h>

#include "garnet/bin/netconnector/socket_address.h"
#include "lib/fxl/logging.h"

namespace netconnector {
namespace mdns {

// SocketAddress with interface index.
class ReplyAddress {
 public:
  // Creates a reply address from an sockaddr_storage struct and an interface
  // index.
  ReplyAddress(const SocketAddress& socket_address, uint32_t interface_index);

  // Creates a reply address from an sockaddr_storage struct and an interface
  // index.
  ReplyAddress(const sockaddr_storage& socket_address,
               uint32_t interface_index);

  const SocketAddress& socket_address() const { return socket_address_; }

  uint32_t interface_index() const { return interface_index_; }

  bool operator==(const ReplyAddress& other) const {
    return socket_address_ == other.socket_address() &&
           interface_index_ == other.interface_index();
  }

  bool operator!=(const ReplyAddress& other) const { return !(*this == other); }

 private:
  SocketAddress socket_address_;
  uint32_t interface_index_;
};

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value);

}  // namespace mdns
}  // namespace netconnector
