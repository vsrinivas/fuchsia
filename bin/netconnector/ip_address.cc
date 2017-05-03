// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <arpa/inet.h>
#include <endian.h>
#include <netdb.h>
#include <sys/socket.h>

#include "apps/netconnector/src/ip_address.h"

namespace netconnector {

// static
const IpAddress IpAddress::kInvalid;

// static
IpAddress IpAddress::FromString(const std::string address_string,
                                sa_family_t family) {
  FTL_DCHECK(family == AF_UNSPEC || family == AF_INET || family == AF_INET6);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = family;
  hints.ai_socktype = 0;
  hints.ai_flags = AI_NUMERICHOST;
  hints.ai_protocol = 0;

  struct addrinfo* addrinfos;
  int result = getaddrinfo(address_string.c_str(), nullptr, &hints, &addrinfos);
  if (result != 0) {
    FTL_DLOG(ERROR) << "Failed to getaddrinfo for address " << address_string
                    << ", errno" << errno;
    return kInvalid;
  }

  if (addrinfos == nullptr) {
    return kInvalid;
  }

  FTL_DCHECK(addrinfos->ai_family == family ||
             (family == AF_UNSPEC && (addrinfos->ai_family == AF_INET ||
                                      addrinfos->ai_family == AF_INET6)));

  IpAddress ip_address = IpAddress(addrinfos->ai_addr);

  freeaddrinfo(addrinfos);

  return ip_address;
}

IpAddress::IpAddress() {
  family_ = AF_UNSPEC;
  std::memset(&v6_, 0, sizeof(v6_));
}

IpAddress::IpAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  family_ = AF_INET;
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&v4_.s_addr);
  bytes[0] = b0;
  bytes[1] = b1;
  bytes[2] = b2;
  bytes[3] = b3;
}

IpAddress::IpAddress(in_addr_t addr) {
  family_ = AF_INET;
  v4_.s_addr = addr;
}

IpAddress::IpAddress(const in_addr& addr) {
  family_ = AF_INET;
  v4_ = addr;
}

IpAddress::IpAddress(uint16_t w0,
                     uint16_t w1,
                     uint16_t w2,
                     uint16_t w3,
                     uint16_t w4,
                     uint16_t w5,
                     uint16_t w6,
                     uint16_t w7) {
  family_ = AF_INET;
  uint16_t* words = v6_.s6_addr16;
  words[0] = htobe16(w0);
  words[1] = htobe16(w1);
  words[2] = htobe16(w2);
  words[3] = htobe16(w3);
  words[4] = htobe16(w4);
  words[5] = htobe16(w5);
  words[6] = htobe16(w6);
  words[7] = htobe16(w7);
}

IpAddress::IpAddress(uint16_t w0, uint16_t w7) {
  family_ = AF_INET;
  std::memset(&v6_, 0, sizeof(v6_));
  uint16_t* words = v6_.s6_addr16;
  words[0] = htobe16(w0);
  words[7] = htobe16(w7);
}

IpAddress::IpAddress(const in6_addr& addr) {
  family_ = AF_INET6;
  v6_ = addr;
}

IpAddress::IpAddress(const sockaddr* addr) {
  FTL_DCHECK(addr != nullptr);
  FTL_DCHECK(addr->sa_family == AF_INET || addr->sa_family == AF_INET6);
  family_ = addr->sa_family;
  if (is_v4()) {
    v4_ = reinterpret_cast<const sockaddr_in*>(addr)->sin_addr;
  } else {
    v6_ = reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr;
  }
}

std::string IpAddress::ToString() const {
  std::ostringstream os;
  os << *this;
  return os.str();
}

std::ostream& operator<<(std::ostream& os, const IpAddress& value) {
  if (!value.is_valid()) {
    return os << "<invalid>";
  }

  if (value.is_v4()) {
    const uint8_t* bytes = value.as_bytes();
    return os << static_cast<int>(bytes[0]) << '.' << static_cast<int>(bytes[1])
              << '.' << static_cast<int>(bytes[2]) << '.'
              << static_cast<int>(bytes[3]);
  } else {
    // IPV6 text representation per RFC 5952:
    // 1) Suppress leading zeros in hex representation of words.
    // 2) Don't use '::' to shorten a just single zero word.
    // 3) Shorten the longest sequence of zero words preferring the leftmost
    //    sequence if there's a tie.
    // 4) Use lower-case hexadecimal.

    const uint16_t* words = value.as_words();

    // Figure out where the longest span of zeros is.
    uint8_t start_of_zeros;
    uint8_t zeros_seen = 0;
    uint8_t start_of_best_zeros = 255;
    // Don't bother if the longest sequence is length 1.
    uint8_t best_zeros_seen = 1;

    for (uint8_t i = 0; i < 8; ++i) {
      if (words[i] == 0) {
        if (zeros_seen == 0) {
          start_of_zeros = i;
        }
        ++zeros_seen;
      } else if (zeros_seen != 0) {
        if (zeros_seen > best_zeros_seen) {
          start_of_best_zeros = start_of_zeros;
          best_zeros_seen = zeros_seen;
        }
        zeros_seen = 0;
      }
    }

    if (zeros_seen > best_zeros_seen) {
      start_of_best_zeros = start_of_zeros;
      best_zeros_seen = zeros_seen;
    }

    os << "[" << std::hex;
    for (uint8_t i = 0; i < 8; ++i) {
      if (i < start_of_best_zeros ||
          i >= start_of_best_zeros + best_zeros_seen) {
        os << words[i];
        if (i != 7) {
          os << ":";
        }
      } else if (i == start_of_best_zeros) {
        if (i == 0) {
          os << "::";
        } else {
          os << ":";  // We just wrote a ':', so we only need one more.
        }
      }
    }
    return os << std::dec << "]";
  }
}

}  // namespace netconnector
