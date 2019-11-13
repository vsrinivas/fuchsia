// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_INET_IP_ADDRESS_H_
#define SRC_LIB_INET_IP_ADDRESS_H_

#include <arpa/inet.h>
#include <fuchsia/net/cpp/fidl.h>

#include "src/lib/fxl/logging.h"

namespace inet {

// Represents a V4 or V6 IP address.
class IpAddress {
 public:
  static const IpAddress kInvalid;
  static const IpAddress kV4Loopback;
  static const IpAddress kV6Loopback;

  // Creates an IpAddress from a string containing a numeric IP address. Returns
  // |kInvalid| if |address_string| cannot be converted into a valid IP address.
  static IpAddress FromString(const std::string address_string, sa_family_t family = AF_UNSPEC);

  // Creates an invalid IP address.
  IpAddress();

  // Creates an IPV4 address from four address bytes.
  IpAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);

  // Creates an IPV4 address from an in_addr_t.
  explicit IpAddress(in_addr_t addr);

  // Creates an IPV4 address from an in_addr struct.
  explicit IpAddress(const in_addr& addr);

  // Creates an IPV6 address from eight address words.
  IpAddress(uint16_t w0, uint16_t w1, uint16_t w2, uint16_t w3, uint16_t w4, uint16_t w5,
            uint16_t w6, uint16_t w7);

  // Creates an IPV6 address from two address words (first and last).
  IpAddress(uint16_t w0, uint16_t w7);

  // Creates an IPV6 address from an in6_addr struct.
  explicit IpAddress(const in6_addr& addr);

  // Creates an address from a sockaddr struct.
  explicit IpAddress(const sockaddr* addr);

  // Creates an address from a sockaddr_storage struct.
  explicit IpAddress(const sockaddr_storage& addr);

  // Creates an address from a fuchsia.net Ipv4Address struct.
  explicit IpAddress(const fuchsia::net::Ipv4Address* addr);

  // Creates an address from a fuchsia.net Ipv6Address struct.
  explicit IpAddress(const fuchsia::net::Ipv6Address* addr);

  // Creates an address from a fuchsia.net IpAddress struct.
  explicit IpAddress(const fuchsia::net::IpAddress* addr);

  // Indicates whether this address is valid.
  bool is_valid() const { return family_ != AF_UNSPEC; }

  // Returns the family of this address: |AF_INET| for V4, |AF_INET6| for V6 and
  // |AF_UNSPEC| for an invalid address.
  sa_family_t family() const { return family_; }

  // Indicates whether this address is a V4 address.
  bool is_v4() const { return family() == AF_INET; }

  // Indicates whether this address is a V6 address.
  bool is_v6() const { return family() == AF_INET6; }

  // Indicates whether this address is a V6 address that is mapped from a V4 address.
  bool is_mapped_from_v4() const;

  // Returns the V4 address from a V6 address that is mapped from a V4 address. Calling this method
  // is only permitted if this address returns true from |is_mapped_from_v4|.
  IpAddress mapped_v4_address() const;

  // Returns the V6 address that is the mapping of this address, which must be a V4 address.
  IpAddress mapped_as_v6() const;

  // Indicates whether this address is a loopback address.
  bool is_loopback() const;

  // Returns this address as an |in_addr|. Only defined for V4 addresses.
  const in_addr& as_in_addr() const {
    FXL_DCHECK(is_v4());
    return v4_;
  }

  // Returns this address as an |in_addr_t|. Only defined for V4 addresses.
  in_addr_t as_in_addr_t() const {
    FXL_DCHECK(is_v4());
    return v4_.s_addr;
  }

  // Returns this address as an |in6_addr|. Only defined for V6 addresses.
  const in6_addr& as_in6_addr() const {
    FXL_DCHECK(is_v6());
    return v6_;
  }

  // Returns a pointer to the bytes that make up this address. |byte_count|
  // indicates the byte count. Not defined for invalid addresses.
  const uint8_t* as_bytes() const {
    FXL_DCHECK(is_valid());
    return v6_.s6_addr;
  }

  // Returns a pointer to the network-order words (big-endian) that make up the
  // address. |word_count| indicates the byte count. Not defined for invalid
  // addresses.
  const uint16_t* as_words() const {
    FXL_DCHECK(is_valid());
    return v6_.s6_addr16;
  }

  // Returns the number of bytes that make up this address. A V4 address is
  // 4 bytes, and a V6 address is 16 bytes. Not defined for invalid addresses.
  size_t byte_count() const {
    FXL_DCHECK(is_valid());
    return is_v4() ? sizeof(in_addr) : sizeof(in6_addr);
  }

  // Returns the number of words that make up this address. A V4 address is
  // 2 words, and a V6 address is 8 words. Not defined for invalid addresses.
  size_t word_count() const {
    FXL_DCHECK(is_valid());
    return byte_count() / sizeof(uint16_t);
  }

  // Returns a string representation of this address. V6 addresses are
  // represented as specified in RFC 5952. For invalid addresses, this method
  // returns "<invalid>".
  std::string ToString() const;

  explicit operator bool() const { return is_valid(); }

  bool operator==(const IpAddress& other) const {
    return family() == other.family() &&
           (!is_valid() || (std::memcmp(as_bytes(), other.as_bytes(), byte_count()) == 0));
  }

  bool operator!=(const IpAddress& other) const { return !(*this == other); }

 private:
  sa_family_t family_;
  union {
    in_addr v4_;
    in6_addr v6_;
  };
};

// Inserts a string representation of |value|. V6 addresses are represented as
// specified in RFC 5952. For invalid addresses, this method inserts
// "<invalid>".
std::ostream& operator<<(std::ostream& os, const IpAddress& value);

}  // namespace inet

template <>
struct std::hash<inet::IpAddress> {
  std::size_t operator()(const inet::IpAddress& address) const noexcept {
    size_t hash = 0;

    if (address.is_valid()) {
      auto word_ptr = address.as_words();
      for (size_t i = 0; i < address.word_count(); ++i) {
        hash = (hash << 1) ^ *word_ptr;
        ++word_ptr;
      }
    }

    return hash;
  }
};

#endif  // SRC_LIB_INET_IP_ADDRESS_H_
