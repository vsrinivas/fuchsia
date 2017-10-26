// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/firebase/status.h"

namespace firebase {

fxl::StringView StatusToString(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::NETWORK_ERROR:
      return "NETWORK_ERROR";
    case Status::PARSE_ERROR:
      return "PARSE_ERROR";
    case Status::SERVER_ERROR:
      return "SERVER_ERROR";
  }
}

std::ostream& operator<<(std::ostream& os, Status status) {
  return os << StatusToString(status);
}

}  // namespace firebase
