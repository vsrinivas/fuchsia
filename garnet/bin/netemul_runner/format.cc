// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "format.h"
#include <iomanip>

namespace netemul {
namespace internal {

void FormatTime(std::ostream* stream, zx_time_t timestamp) {
  if (stream) {
    *stream << "[" << std::setfill('0') << std::setw(6)
            << timestamp / 1000000000 << "." << std::setfill('0')
            << std::setw(6) << (timestamp / 1000) % 1000000 << "]";
  }
}

}  // namespace internal
}  // namespace netemul
