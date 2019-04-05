// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <netinet/in.h>
#include <iosfwd>
#include "src/connectivity/overnet/lib/vocabulary/optional.h"

namespace overnet {

union IpAddr {
  sockaddr_in ipv4;
  sockaddr_in6 ipv6;
  sockaddr addr;

  Optional<IpAddr> WithPort(uint16_t port) const;

  static IpAddr AnyIpv4();
  static IpAddr AnyIpv6();

  Optional<IpAddr> AsIpv6() const;
  uint16_t port() const {
    switch (addr.sa_family) {
      case AF_INET:
        return htons(ipv4.sin_port);
      case AF_INET6:
        return htons(ipv6.sin6_port);
      default:
        return 0;
    }
  }

  IpAddr() {}
  constexpr IpAddr(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e,
                   uint16_t f, uint16_t g, uint16_t h, uint16_t port)
      : ipv6{.sin6_family = AF_INET6,
             .sin6_addr = {{{
                 static_cast<unsigned char>(a >> 8),
                 static_cast<unsigned char>(a & 0xff),
                 static_cast<unsigned char>(b >> 8),
                 static_cast<unsigned char>(b & 0xff),
                 static_cast<unsigned char>(c >> 8),
                 static_cast<unsigned char>(c & 0xff),
                 static_cast<unsigned char>(d >> 8),
                 static_cast<unsigned char>(d & 0xff),
                 static_cast<unsigned char>(e >> 8),
                 static_cast<unsigned char>(e & 0xff),
                 static_cast<unsigned char>(f >> 8),
                 static_cast<unsigned char>(f & 0xff),
                 static_cast<unsigned char>(g >> 8),
                 static_cast<unsigned char>(g & 0xff),
                 static_cast<unsigned char>(h >> 8),
                 static_cast<unsigned char>(h & 0xff),
             }}},
             .sin6_port = uint16_t((port >> 8) | (port << 8))} {}
  constexpr IpAddr(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
      : ipv4{.sin_family = AF_INET,
             .sin_addr = {(uint32_t(d) << 24) | (uint32_t(c) << 16) |
                          (uint32_t(b) << 8) | uint32_t(a)},
             .sin_port = uint16_t((port >> 8) | (port << 8))} {}
};

std::ostream& operator<<(std::ostream& out, IpAddr addr);

class HashIpAddr {
 public:
  size_t operator()(const IpAddr& addr) const;
};

class EqIpAddr {
 public:
  bool operator()(const IpAddr& a, const IpAddr& b) const;
};

inline bool operator==(const IpAddr& a, const IpAddr& b) {
  return EqIpAddr()(a, b);
}

}  // namespace overnet
