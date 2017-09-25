// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/convert_status.h"

namespace cloud_provider_firebase {

cloud_provider::Status ConvertInternalStatus(Status status) {
  switch (status) {
    case Status::OK:
      return cloud_provider::Status::OK;
    case Status::ARGUMENT_ERROR:
      return cloud_provider::Status::ARGUMENT_ERROR;
    case Status::NETWORK_ERROR:
      return cloud_provider::Status::NETWORK_ERROR;
    case Status::NOT_FOUND:
      return cloud_provider::Status::NOT_FOUND;
    case Status::INTERNAL_ERROR:
      return cloud_provider::Status::INTERNAL_ERROR;
    case Status::PARSE_ERROR:
      return cloud_provider::Status::PARSE_ERROR;
    case Status::SERVER_ERROR:
      return cloud_provider::Status::SERVER_ERROR;
  }
}

}  // namespace cloud_provider_firebase
