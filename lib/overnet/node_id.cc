// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "node_id.h"
#include <iomanip>
#include <sstream>

namespace overnet {

std::ostream& operator<<(std::ostream& out, NodeId node_id) {
  return out << node_id.ToString();
}

std::string NodeId::ToString() const {
  std::ostringstream tmp;
  tmp << "[";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 48) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 32) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << ((id_ >> 16) & 0xffff);
  tmp << "_";
  tmp << std::hex << std::setfill('0') << std::setw(4);
  tmp << (id_ & 0xffff);
  tmp << "]";
  return tmp.str();
}

}  // namespace overnet
