// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <iostream>

#include "garnet/bin/mdns/formatting.h"

namespace mdns {
namespace {

int ostream_indent_index() {
  static int i = std::ios_base::xalloc();
  return i;
}

}  // namespace

std::ostream& begl(std::ostream& os) {
  for (long i = 0; i < os.iword(ostream_indent_index()); i++) {
    os << "    ";
  }
  return os;
}

std::ostream& indent(std::ostream& os) {
  ++os.iword(ostream_indent_index());
  return os;
}

std::ostream& outdent(std::ostream& os) {
  --os.iword(ostream_indent_index());
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const netconnector::MdnsServiceInstance& value) {
  os << value.service_name << " " << value.instance_name;
  os << indent;

  if (value.v4_address) {
    os << "\n" << begl << "IPv4 address: " << *value.v4_address;
  }

  if (value.v6_address) {
    os << "\n" << begl << "IPv6 address: " << *value.v6_address;
  }

  if (value.text) {
    os << "\n" << begl << "text: " << value.text;
  }

  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const netstack::SocketAddress& value) {
  FXL_DCHECK(value.addr);

  if (value.addr->family == netstack::NetAddressFamily::UNSPECIFIED) {
    return os << "<unspecified>";
  }

  if (value.addr->family == netstack::NetAddressFamily::IPV4) {
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(value.addr->ipv4.data());
    os << static_cast<int>(bytes[0]) << '.' << static_cast<int>(bytes[1]) << '.'
       << static_cast<int>(bytes[2]) << '.' << static_cast<int>(bytes[3]);
  } else {
    // IPV6 text representation per RFC 5952:
    // 1) Suppress leading zeros in hex representation of words.
    // 2) Don't use '::' to shorten a just single zero word.
    // 3) Shorten the longest sequence of zero words preferring the leftmost
    //    sequence if there's a tie.
    // 4) Use lower-case hexadecimal.

    const uint16_t* words =
        reinterpret_cast<const uint16_t*>(value.addr->ipv6.data());

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
    os << std::dec << "]";
  }

  return os << ":" << value.port;
}

}  // namespace mdns
