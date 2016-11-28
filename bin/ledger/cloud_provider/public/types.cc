// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/public/types.h"

namespace cloud_provider {

ftl::StringView StatusToString(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::ARGUMENT_ERROR:
      return "ARGUMENT_ERROR";
    case Status::NETWORK_ERROR:
      return "NETWORK_ERROR";
    case Status::NOT_FOUND:
      return "NOT_FOUND";
    case Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case Status::UNKNOWN_ERROR:
      return "UNKNOWN_ERROR";
  }
}

std::ostream& operator<<(std::ostream& os, Status status) {
  return os << StatusToString(status);
}

}  // namespace cloud_provider
