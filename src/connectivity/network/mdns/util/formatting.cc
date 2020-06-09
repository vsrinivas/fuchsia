// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/util/formatting.h"

#include <iomanip>
#include <iostream>

namespace mdns {

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& value) {
  if (value.empty()) {
    return os << "<empty>";
  }

  int index = 0;
  for (auto& element : value) {
    os << fostr::NewLine << "[" << index++ << "] " << element;
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::mdns::ServiceInstance& value) {
  os << value.service << " " << value.instance;
  os << fostr::Indent;

  if (!value.endpoints.empty()) {
    os << fostr::NewLine << "endpoints: " << value.endpoints;
  }

  if (!value.text.empty()) {
    os << fostr::NewLine << "text: " << value.text;
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

  os << "[" << std::hex;
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

  return os << std::dec << "]";
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::IpAddress& value) {
  switch (value.Which()) {
    case fuchsia::net::IpAddress::Tag::Invalid:
      os << "<invalid>";
      break;
    case fuchsia::net::IpAddress::Tag::kIpv4:
      os << value.ipv4();
      break;
    case fuchsia::net::IpAddress::Tag::kIpv6:
      os << value.ipv6();
      break;
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::Endpoint& value) {
  if (value.addr.Which() == fuchsia::net::IpAddress::Tag::Invalid) {
    return os << "<unspecified>";
  }

  return os << value.addr << ":" << value.port;
}

}  // namespace mdns
