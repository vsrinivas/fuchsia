// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "ostream.h"

#include <lib/memalloc/range.h>

#include <algorithm>
#include <ostream>
#include <string>

namespace memalloc {

namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

}  // namespace

std::ostream& operator<<(std::ostream& stream, MemRange range) {
  stream << ToString(range.type) << ": ";
  if (range.size == 0) {
    stream << "Ø";
  } else {
    stream << "[" << range.addr << ", " << (range.addr + std::min(kMax - range.addr, range.size))
           << ")";
  }
  return stream;
}

}  // namespace memalloc
