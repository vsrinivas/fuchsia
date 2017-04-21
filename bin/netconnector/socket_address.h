// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ostream>

#include <arpa/inet.h>
#include <sys/socket.h>

#include "apps/netconnector/src/ip_address.h"
#include "apps/netconnector/src/ip_port.h"
#include "lib/ftl/logging.h"

namespace netconnector {

// Represents a V4 or V6 socket address.
class SocketAddress {
 public:
  static const SocketAddress kInvalid;

  // Creates an invalid socket.
  SocketAddress();

  // Creates an IPV4 socket address from four address bytes and an IpPort.
  SocketAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, IpPort port);

  // Creates an IPV4 socket address from an in_addr_t and an IpPort.
  SocketAddress(in_addr_t addr, IpPort port);

  // Creates an IPV4 socket address from an sockaddr_in struct.
  explicit SocketAddress(const sockaddr_in& addr);

  // Creates an IPV6 socket address from eight address words and an IpPort.
  SocketAddress(uint16_t w0,
                uint16_t w1,
                uint16_t w2,
                uint16_t w3,
                uint16_t w4,
                uint16_t w5,
                uint16_t w6,
                uint16_t w7,
                IpPort port);

  // Creates an IPV6 socket address from two address words and an IpPort.
  SocketAddress(uint16_t w0, uint16_t w7, IpPort port);

  // Creates an IPV6 socket address from an in6_addr struct and an IpPort.
  SocketAddress(const in6_addr& addr, IpPort port);

  // Creates an IPV6 socket address from an sockaddr_in6 struct.
  explicit SocketAddress(const sockaddr_in6& addr);

  // Creates a socket address from a IpAddress and an IpPort.
  SocketAddress(const IpAddress& addr, IpPort port);

  // Creates a socket address from an sockaddr_storage struct.
  explicit SocketAddress(const sockaddr_storage& addr);

  bool is_valid() const { return family() != AF_UNSPEC; }

  sa_family_t family() const { return v4_.sin_family; }

  bool is_v4() const { return family() == AF_INET; }

  bool is_v6() const { return family() == AF_INET6; }

  IpAddress address() const {
    return is_v4() ? IpAddress(v4_.sin_addr) : IpAddress(v6_.sin6_addr);
  }

  IpPort port() const { return IpPort::From_in_port_t(v4_.sin_port); }

  const sockaddr_in& as_sockaddr_in() const {
    FTL_DCHECK(is_v4());
    return v4_;
  }

  const sockaddr_in6& as_sockaddr_in6() const {
    FTL_DCHECK(is_v6());
    return v6_;
  }

  const sockaddr* as_sockaddr() const {
    return reinterpret_cast<const sockaddr*>(&v4_);
  }

  socklen_t socklen() const { return is_v4() ? sizeof(v4_) : sizeof(v6_); }

  std::string ToString() const;

  explicit operator bool() const { return is_valid(); }

  bool operator==(const SocketAddress& other) const {
    return is_v4() == other.is_v4() &&
           std::memcmp(as_sockaddr(), other.as_sockaddr(), socklen()) == 0;
  }

  bool operator!=(const SocketAddress& other) const {
    return !(*this == other);
  }

 private:
  union {
    sockaddr_in v4_;
    sockaddr_in6 v6_;
  };
};

std::ostream& operator<<(std::ostream& os, const SocketAddress& value);

}  // namespace netconnector
