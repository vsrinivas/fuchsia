// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/id.h"

#include <sstream>

namespace scenic_impl {

GlobalId::operator bool() const {
  static const GlobalId kNoGlobalId;
  return *this != kNoGlobalId;
}

GlobalId::operator std::string() const {
  std::ostringstream out;
  out << *this;
  return out.str();
}

bool operator==(const GlobalId& lhs, const GlobalId& rhs) {
  return lhs.session_id == rhs.session_id && lhs.resource_id == rhs.resource_id;
}

bool operator!=(const GlobalId& lhs, const GlobalId& rhs) { return !(lhs == rhs); }

bool operator<(const GlobalId& lhs, const GlobalId& rhs) {
  return lhs.session_id < rhs.session_id ||
         (lhs.session_id == rhs.session_id && lhs.resource_id < rhs.resource_id);
}

std::ostream& operator<<(std::ostream& os, const GlobalId& value) {
  return os << value.session_id << "-" << value.resource_id;
}

}  // namespace scenic_impl
