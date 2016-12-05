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
    case Status::PARSE_ERROR:
      return "PARSE_ERROR";
    case Status::SERVER_ERROR:
      return "SERVER_ERROR";
  }
}

std::ostream& operator<<(std::ostream& os, Status status) {
  return os << StatusToString(status);
}

Status ConvertFirebaseStatus(firebase::Status firebase_status) {
  switch (firebase_status) {
    case firebase::Status::OK:
      return Status::OK;
    case firebase::Status::NETWORK_ERROR:
      return Status::NETWORK_ERROR;
    case firebase::Status::PARSE_ERROR:
      return Status::PARSE_ERROR;
    case firebase::Status::SERVER_ERROR:
      return Status::SERVER_ERROR;
  }
}

}  // namespace cloud_provider
