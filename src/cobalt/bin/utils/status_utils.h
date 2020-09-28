// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_UTILS_STATUS_UTILS_H_
#define GARNET_BIN_COBALT_UTILS_STATUS_UTILS_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include <string>

namespace cobalt {

inline std::string StatusToString(fuchsia::cobalt::Status status) {
  switch (status) {
    case fuchsia::cobalt::Status::OK:
      return "OK";
    case fuchsia::cobalt::Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case fuchsia::cobalt::Status::EVENT_TOO_BIG:
      return "EVENT_TOO_BIG";
    case fuchsia::cobalt::Status::BUFFER_FULL:
      return "BUFFER_FULL";
    case fuchsia::cobalt::Status::SHUT_DOWN:
      return "SHUT_DOWN";
    case fuchsia::cobalt::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_UTILS_STATUS_UTILS_H_
