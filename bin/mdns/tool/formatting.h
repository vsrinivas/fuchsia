// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_TOOL_FORMATTING_H_
#define GARNET_BIN_MDNS_TOOL_FORMATTING_H_

#include <iomanip>
#include <iostream>

#include <fuchsia/mdns/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>

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
                         const fuchsia::mdns::MdnsServiceInstance& value);
std::ostream& operator<<(std::ostream& os,
                         const fuchsia::netstack::SocketAddress& value);

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_TOOL_FORMATTING_H_
