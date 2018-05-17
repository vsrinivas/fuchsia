// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "slice.h"
#include <iomanip>
#include <sstream>

namespace overnet {

std::ostream& operator<<(std::ostream& out, const Slice& slice) {
  bool first = true;
  std::ostringstream temp;
  for (auto b : slice) {
    if (!first) temp << ' ';
    temp << std::hex << std::setfill('0') << std::setw(2)
         << static_cast<unsigned>(b);
    first = false;
  }
  return out << '[' << temp.str() << ']';
}

std::ostream& operator<<(std::ostream& out, const Chunk& chunk) {
  return out << "@" << chunk.offset << chunk.slice;
}

}  // namespace overnet
