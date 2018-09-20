// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status.h"

namespace overnet {

namespace status_impl {
const std::string empty_string;
}

const char *StatusCodeString(StatusCode code) {
  switch (code) {
    case StatusCode::OK:
      return "OK";
    case StatusCode::CANCELLED:
      return "CANCELLED";
    case StatusCode::UNKNOWN:
      return "UNKNOWN";
    case StatusCode::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case StatusCode::DEADLINE_EXCEEDED:
      return "DEADLINE_EXCEEDED";
    case StatusCode::NOT_FOUND:
      return "NOT_FOUND";
    case StatusCode::ALREADY_EXISTS:
      return "ALREADY_EXISTS";
    case StatusCode::PERMISSION_DENIED:
      return "PERMISSION_DENIED";
    case StatusCode::UNAUTHENTICATED:
      return "UNAUTHENTICATED";
    case StatusCode::RESOURCE_EXHAUSTED:
      return "RESOURCE_EXHAUSTED";
    case StatusCode::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case StatusCode::ABORTED:
      return "ABORTED";
    case StatusCode::OUT_OF_RANGE:
      return "OUT_OF_RANGE";
    case StatusCode::UNIMPLEMENTED:
      return "UNIMPLEMENTED";
    case StatusCode::INTERNAL:
      return "INTERNAL";
    case StatusCode::UNAVAILABLE:
      return "UNAVAILABLE";
    case StatusCode::DATA_LOSS:
      return "DATA_LOSS";
  }
  return "UNKNOWN_STATUS_CODE";
}

}  // namespace overnet
