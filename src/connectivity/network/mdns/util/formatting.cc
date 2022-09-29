// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/util/formatting.h"

#include <iomanip>
#include <iostream>

#include "src/lib/fostr/hex_dump.h"
#include "src/lib/fostr/zx_types.h"

namespace mdns {

std::ostream& operator<<(std::ostream& os, const std::vector<uint8_t>& value) {
  if (value.empty()) {
    return os << "<empty>";
  }

  if (std::all_of(value.cbegin(), value.cend(), [](uint8_t b) { return b >= ' ' && b <= '~'; })) {
    std::cout << "\"" << std::string(value.begin(), value.end()) << "\"";
  } else {
    std::cout << fostr::NewLine << fostr::HexDump(value);
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::mdns::ServiceInstance& value) {
  os << fostr::Indent;

  if (value.has_addresses() && !value.addresses().empty()) {
    os << fostr::NewLine << "addresses:" << value.addresses();
  }

  if (value.has_text_strings() && !value.text_strings().empty()) {
    os << fostr::NewLine << "text:" << value.text();
  }

  if (value.has_target()) {
    os << fostr::NewLine << "target: " << value.target();
  }

  if (value.has_srv_priority() && value.srv_priority() != 0) {
    os << fostr::NewLine << "srv priority: " << value.srv_priority();
  }

  if (value.has_srv_weight() && value.srv_weight() != 0) {
    os << fostr::NewLine << "srv weight: " << value.srv_weight();
  }

  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv4Address& value) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(value.addr.data());
  return os << static_cast<int>(bytes[0]) << '.' << static_cast<int>(bytes[1]) << '.'
            << static_cast<int>(bytes[2]) << '.' << static_cast<int>(bytes[3]);
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv6Address& value) {
  // IPV6 text representation per RFC 5952:
  // 1) Suppress leading zeros in hex representation of words.
  // 2) Don't use '::' to shorten a just single zero word.
  // 3) Shorten the longest sequence of zero words preferring the leftmost
  //    sequence if there's a tie.
  // 4) Use lower-case hexadecimal.

  const uint16_t* words = reinterpret_cast<const uint16_t*>(value.addr.data());

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

  os << std::hex;
  for (uint8_t i = 0; i < 8; ++i) {
    if (i < start_of_best_zeros || i >= start_of_best_zeros + best_zeros_seen) {
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

  return os << std::dec;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::IpAddress& value) {
  if (value.is_ipv4()) {
    return os << value.ipv4();
  } else {
    return os << value.ipv6();
  }
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv4SocketAddress& value) {
  return os << value.address << ":" << value.port;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv6SocketAddress& value) {
  return os << "[" << value.address << "]:" << value.port;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::SocketAddress& value) {
  if (value.is_ipv4()) {
    return os << value.ipv4();
  } else {
    return os << value.ipv6();
  }
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::mdns::HostAddress& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "address: " << value.address;
  os << fostr::NewLine << "interface: " << value.interface;
  os << fostr::NewLine << "ttl: " << zx::duration(value.ttl);
  return os << fostr::Outdent;
}

}  // namespace mdns
