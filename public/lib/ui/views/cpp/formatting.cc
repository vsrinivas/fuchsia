// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/views/cpp/formatting.h"

#include <ostream>

namespace mozart {

std::ostream& operator<<(std::ostream& os, const ViewToken& value) {
  return os << "<V" << value.value << ">";
}

std::ostream& operator<<(std::ostream& os, const ViewTreeToken& value) {
  return os << "<T" << value.value << ">";
}

std::ostream& operator<<(std::ostream& os, const ViewInfo& value) {
  return os << "{}";
}

std::ostream& operator<<(std::ostream& os, const ViewProperties& value) {
  return os << "{view_layout=" << value.view_layout << "}";
}

std::ostream& operator<<(std::ostream& os, const ViewLayout& value) {
  return os << "{size=" << value.size << ", inset=" << value.inset << "}";
}

}  // namespace mozart
