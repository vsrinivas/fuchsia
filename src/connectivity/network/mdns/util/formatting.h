// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_FORMATTING_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_FORMATTING_H_

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/mdns/cpp/fidl.h>

#include <iomanip>
#include <iostream>

#include "src/lib/fostr/indent.h"

namespace mdns {

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::VectorPtr<T>& value) {
  if (value->size() == 0) {
    return os << "<empty>";
  }

  os << fostr::Indent;

  for (const T& element : *value) {
    os << fostr::NewLine << element;
  }

  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const fuchsia::net::mdns::ServiceInstance& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv4Address& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv6Address& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::IpAddress& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv4SocketAddress& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::Ipv6SocketAddress& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::SocketAddress& value);
std::ostream& operator<<(std::ostream& os, const fuchsia::net::mdns::HostAddress& value);

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& value) {
  if (value.empty()) {
    return os << "<empty>";
  }

  os << fostr::Indent;

  for (auto& element : value) {
    os << fostr::NewLine << element;
  }

  return os << fostr::Outdent;
}

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_UTIL_FORMATTING_H_
