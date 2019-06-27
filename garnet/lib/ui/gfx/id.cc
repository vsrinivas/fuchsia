// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/id.h"

namespace scenic_impl {

GlobalId::operator bool() {
  static const GlobalId kNoGlobalId;
  return *this != kNoGlobalId;
}

bool operator==(const GlobalId& lhs, const GlobalId& rhs) {
  return lhs.session_id == rhs.session_id && lhs.resource_id == rhs.resource_id;
}

bool operator!=(const GlobalId& lhs, const GlobalId& rhs) { return !(lhs == rhs); }

std::ostream& operator<<(std::ostream& os, const GlobalId& value) {
  return os << value.session_id << "-" << value.resource_id;
}

}  // namespace scenic_impl
