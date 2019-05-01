// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_FORMATTING_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_FORMATTING_H_

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/mdns/cpp/fidl.h>

#include <iomanip>
#include <iostream>

#include "lib/fostr/indent.h"

namespace mdns {

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::VectorPtr<T>& value) {
  if (value->size() == 0) {
    return os << "<empty>";
  }

  int index = 0;
  for (const T& element : *value) {
    os << fostr::NewLine << "[" << index++ << "] " << element;
  }

  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::net::mdns::ServiceInstance& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::net::Ipv4Address& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::net::Ipv6Address& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::net::IpAddress& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::Endpoint& value);

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_FORMATTING_H_
