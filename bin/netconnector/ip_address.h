// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arpa/inet.h>

#include "lib/ftl/logging.h"

namespace netconnector {

// Represents a V4 or V6 IP address.
class IpAddress {
 public:
  static const IpAddress kInvalid;

  // Creates an IpAddress from a string containing a numeric IP address. Returns
  // |kInvalid| if |address_string| cannot be converted into a valid IP address.
  static IpAddress FromString(const std::string address_string,
                              sa_family_t family = AF_UNSPEC);

  // Creates an invalid IP address.
  IpAddress();

  // Creates an IPV4 address from four address bytes.
  IpAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);

  // Creates an IPV4 address from an in_addr_t.
  explicit IpAddress(in_addr_t addr);

  // Creates an IPV4 address from an in_addr struct.
  explicit IpAddress(const in_addr& addr);

  // Creates an IPV6 address from eight address words.
  IpAddress(uint16_t w0,
            uint16_t w1,
            uint16_t w2,
            uint16_t w3,
            uint16_t w4,
            uint16_t w5,
            uint16_t w6,
            uint16_t w7);

  // Creates an IPV6 address from two address words (first and last).
  IpAddress(uint16_t w0, uint16_t w7);

  // Creates an IPV6 address from an in6_addr struct.
  explicit IpAddress(const in6_addr& addr);

  // Creates an address from a sockaddr struct.
  explicit IpAddress(const sockaddr* addr);

  bool is_valid() const { return family_ != AF_UNSPEC; }

  sa_family_t family() const { return family_; }

  bool is_v4() const { return family() == AF_INET; }

  bool is_v6() const { return family() == AF_INET6; }

  const in_addr& as_in_addr() const {
    FTL_DCHECK(is_v4());
    return v4_;
  }

  in_addr_t as_in_addr_t() const {
    FTL_DCHECK(is_v4());
    return v4_.s_addr;
  }

  const in6_addr& as_in6_addr() const {
    FTL_DCHECK(is_v6());
    return v6_;
  }

  const uint8_t* as_bytes() const { return v6_.__in6_union.__s6_addr; }

  const uint16_t* as_words() const { return v6_.__in6_union.__s6_addr16; }

  size_t byte_count() const {
    return is_v4() ? sizeof(in_addr) : sizeof(in6_addr);
  }

  size_t word_count() const { return byte_count() / sizeof(uint16_t); }

  std::string ToString() const;

  explicit operator bool() const { return is_valid(); }

  bool operator==(const IpAddress& other) const {
    return is_v4() == other.is_v4() &&
           std::memcmp(as_bytes(), other.as_bytes(), byte_count()) == 0;
  }

  bool operator!=(const IpAddress& other) const { return !(*this == other); }

 private:
  sa_family_t family_;
  union {
    in_addr v4_;
    in6_addr v6_;
  };
};

std::ostream& operator<<(std::ostream& os, const IpAddress& value);

}  // namespace netconnector
