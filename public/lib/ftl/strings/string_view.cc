// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/strings/string_view.h"

#include <algorithm>

#include <string.h>

namespace ftl {

int StringView::compare(StringView other) {
  size_t len = std::min(size_, other.size_);
  int retval = strncmp(data_, other.data_, len);
  if (retval == 0) {
    if (size_ == other.size_) {
      return 0;
    }
    return size_ < other.size_ ? -1 : 1;
  }
  return retval;
}

bool operator==(StringView lhs, StringView rhs) {
  if (lhs.size() != rhs.size())
    return false;
  return lhs.compare(rhs) == 0;
}

bool operator!=(StringView lhs, StringView rhs) {
  if (lhs.size() != rhs.size())
    return true;
  return lhs.compare(rhs) != 0;
}

bool operator<(StringView lhs, StringView rhs) {
  return lhs.compare(rhs) < 0;
}

bool operator>(StringView lhs, StringView rhs) {
  return lhs.compare(rhs) > 0;
}

bool operator<=(StringView lhs, StringView rhs) {
  return lhs.compare(rhs) <= 0;
}

bool operator>=(StringView lhs, StringView rhs) {
  return lhs.compare(rhs) >= 0;
}

std::ostream& operator<<(std::ostream& o, StringView string_view) {
  o.write(string_view.data(), static_cast<std::streamsize>(string_view.size()));
  return o;
}

}  // namespace ftl
