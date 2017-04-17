// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iomanip>
#include <iostream>

#include "apps/netconnector/src/mdns/dns_message.h"

namespace netconnector {
namespace mdns {

inline std::ostream& begl(std::ostream& os);
inline std::ostream& indent(std::ostream& os);
inline std::ostream& outdent(std::ostream& os);

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& value) {
  if (value.size() == 0) {
    return os << "<empty>" << std::endl;
  }

  int index = 0;
  for (const T& element : value) {
    os << std::endl << begl << "[" << index++ << "] " << element;
  }

  return os;
}

template <>
std::ostream& operator<<(std::ostream& os, const std::vector<uint8_t>& value);

std::ostream& operator<<(std::ostream& os, DnsType value);
std::ostream& operator<<(std::ostream& os, DnsClass value);
std::ostream& operator<<(std::ostream& os, const DnsName& value);
std::ostream& operator<<(std::ostream& os, const DnsV4Address& value);
std::ostream& operator<<(std::ostream& os, const DnsV6Address& value);
std::ostream& operator<<(std::ostream& os, const DnsHeader& value);
std::ostream& operator<<(std::ostream& os, const DnsQuestion& value);
std::ostream& operator<<(std::ostream& os, const DnsResource& value);
std::ostream& operator<<(std::ostream& os, const DnsMessage& value);

}  // namespace mdns
}  // namespace netconnector
