// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/string_piece.h>

#include <mxtl/algorithm.h>

namespace mxtl {

int StringPiece::compare(StringPiece other) const {
    size_t len = min(length_, other.length_);
    int retval = memcmp(ptr_, other.ptr_, len);
    if (retval == 0) {
        if (length_ == other.length_) {
            return 0;
        }
        return length_ < other.length_ ? -1 : 1;
    }
    return retval;
}

bool operator==(StringPiece lhs, StringPiece rhs) {
  if (lhs.length() != rhs.length())
    return false;
  return lhs.compare(rhs) == 0;
}

bool operator!=(StringPiece lhs, StringPiece rhs) {
  if (lhs.length() != rhs.length())
    return true;
  return lhs.compare(rhs) != 0;
}

}  // namespace mxtl
