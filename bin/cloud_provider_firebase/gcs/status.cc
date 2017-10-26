// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/gcs/status.h"

namespace gcs {

fxl::StringView StatusToString(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case Status::NETWORK_ERROR:
      return "NETWORK_ERROR";
    case Status::NOT_FOUND:
      return "NOT_FOUND";
    case Status::OBJECT_ALREADY_EXISTS:
      return "OBJECT_ALREADY_EXISTS";
    case Status::PARSE_ERROR:
      return "PARSE_ERROR";
    case Status::SERVER_ERROR:
      return "SERVER_ERROR";
  }
}

}  // namespace gcs
