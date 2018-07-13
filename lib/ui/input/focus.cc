// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/focus.h"

namespace scenic {
namespace input {

std::ostream& operator<<(std::ostream& os, const ViewId& value) {
  os << "ViewId: [";
  if (value.session_id == 0u && value.resource_id == 0u) {
    os << "no-view";
  } else {
    os << value.session_id << ", " << value.resource_id;
  }
  return os << "]";
}

std::ostream& operator<<(std::ostream& os, const Focus& value) {
  os << "Focus: [";
  if (value.chain.empty()) {
    os << "no-focus";
  } else {
    for (size_t i = 0; i < value.chain.size(); ++i) {
      os << value.chain[i];
      if (i + 1 < value.chain.size()) {
        os << ", ";
      }
    }
  }
  return os << "]";
}

bool operator==(const ViewId& lhs, const ViewId& rhs) {
  return lhs.session_id == rhs.session_id && lhs.resource_id == rhs.resource_id;
}

bool operator!=(const ViewId& lhs, const ViewId& rhs) { return !(lhs == rhs); }

}  // namespace input
}  // namespace scenic
