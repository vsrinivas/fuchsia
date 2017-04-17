// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/ip_port.h"

namespace netconnector {

IpPort::IpPort() : value_(0) {}

IpPort::IpPort(uint8_t b0, uint8_t b1) {
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&value_);
  bytes[0] = b0;
  bytes[1] = b1;
}

std::ostream& operator<<(std::ostream& os, IpPort value) {
  return os << value.as_uint16_t();
}

}  // namespace netconnector
