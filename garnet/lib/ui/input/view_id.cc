// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/view_id.h"

namespace scenic_impl {
namespace input {

using escher::operator<<;

std::ostream& operator<<(std::ostream& os, const ViewStack::Entry& value) {
  return os << "Entry: [" << value.session_id << ", GlobalTransform=\n"
            << value.global_transform << "\n]";
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
}  // namespace scenic_impl
