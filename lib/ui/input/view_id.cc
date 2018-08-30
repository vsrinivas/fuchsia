// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/view_id.h"

namespace scenic {
namespace input {

ViewId::operator bool() {
  static const ViewId kNoView;
  return *this != kNoView;
}

bool operator==(const ViewId& lhs, const ViewId& rhs) {
  return lhs.session_id == rhs.session_id && lhs.resource_id == rhs.resource_id;
}

bool operator!=(const ViewId& lhs, const ViewId& rhs) { return !(lhs == rhs); }

std::ostream& operator<<(std::ostream& os, const ViewId& value) {
  os << "ViewId: [";
  if (value.session_id == 0u && value.resource_id == 0u) {
    os << "no-view";
  } else {
    os << value.session_id << ", " << value.resource_id;
  }
  return os << "]";
}

std::ostream& operator<<(std::ostream& os, const ViewStack& value) {
  os << "ViewStack: [";
  if (value.stack.empty()) {
    os << "empty";
  } else {
    for (size_t i = 0; i < value.stack.size(); ++i) {
      os << value.stack[i];
      if (i + 1 < value.stack.size()) {
        os << ", ";
      }
    }
  }
  return os << "]";
}

}  // namespace input
}  // namespace scenic
