// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "format.h"

#include <iomanip>
#include <vector>

namespace netemul {
namespace internal {

void FormatTime(std::ostream* stream, const zx_time_t timestamp) {
  if (!stream) {
    return;
  }

  *stream << "[" << std::setfill('0') << std::setw(6) << timestamp / 1000000000
          << "." << std::setfill('0') << std::setw(6)
          << (timestamp / 1000) % 1000000 << "]";
}

void FormatTags(std::ostream* stream, const std::vector<std::string>& tags) {
  if (!stream) {
    return;
  }

  auto it = tags.begin();

  *stream << "[";

  while (it != tags.end()) {
    *stream << *it;

    it = std::next(it);

    if (it != tags.end()) {
      *stream << ",";
    }
  }

  *stream << "]";
}

void FormatLogLevel(std::ostream* stream, const int32_t severity) {
  if (!stream) {
    return;
  }

  switch (severity) {
    case 0:
      *stream << "[INFO]";
      break;

    case 1:
      *stream << "[WARNING]";
      break;

    case 2:
      *stream << "[ERROR]";
      break;

    case 3:
      *stream << "[FATAL]";
      break;

    default:
      if (severity > 3) {
        *stream << "[INVALID]";
      } else {
        *stream << "[VLOG(" << severity << ")]";
      }
  }
}

}  // namespace internal
}  // namespace netemul
