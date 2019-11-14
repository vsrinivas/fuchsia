// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/view_stack.h"

namespace scenic_impl::input {

using escher::operator<<;

std::ostream& operator<<(std::ostream& os, const ViewStack::Entry& value) {
  return os << "Entry: [ViewRefKoid=" << value.view_ref_koid << ", Transform=\n"
            << value.transform << "\n]";
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

}  // namespace scenic_impl::input
