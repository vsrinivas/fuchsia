// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iomanip>
#include <iostream>

#include "lib/netconnector/fidl/mdns.fidl.h"
#include "lib/netstack/fidl/net_address.fidl.h"

namespace mdns {

std::ostream& begl(std::ostream& os);
std::ostream& indent(std::ostream& os);
std::ostream& outdent(std::ostream& os);

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::Array<T>& value) {
  if (value.size() == 0) {
    return os << "<empty>";
  }

  int index = 0;
  for (const T& element : value) {
    os << "\n" << begl << "[" << index++ << "] " << element;
  }

  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const netconnector::MdnsServiceInstance& value);
std::ostream& operator<<(std::ostream& os,
                         const netstack::SocketAddress& value);

}  // namespace mdns
