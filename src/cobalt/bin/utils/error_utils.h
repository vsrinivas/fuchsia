// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_UTILS_ERROR_UTILS_H_
#define SRC_COBALT_BIN_UTILS_ERROR_UTILS_H_

#include <fuchsia/metrics/cpp/fidl.h>

#include <string>

namespace cobalt {

inline std::string ErrorToString(fuchsia::metrics::Error error) {
  switch (error) {
    case fuchsia::metrics::Error::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case fuchsia::metrics::Error::EVENT_TOO_BIG:
      return "EVENT_TOO_BIG";
    case fuchsia::metrics::Error::BUFFER_FULL:
      return "BUFFER_FULL";
    case fuchsia::metrics::Error::SHUT_DOWN:
      return "SHUT_DOWN";
    case fuchsia::metrics::Error::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
}

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_UTILS_ERROR_UTILS_H_
