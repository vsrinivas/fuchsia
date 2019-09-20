// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/status.h"

#include "src/ledger/bin/fidl/include/types.h"

namespace cloud_sync {

bool IsPermanentError(cloud_provider::Status status) {
  switch (status) {
    case cloud_provider::Status::OK:
    case cloud_provider::Status::AUTH_ERROR:
    case cloud_provider::Status::NETWORK_ERROR:
      return false;
    case cloud_provider::Status::ARGUMENT_ERROR:
    case cloud_provider::Status::INTERNAL_ERROR:
    case cloud_provider::Status::NOT_FOUND:
    case cloud_provider::Status::PARSE_ERROR:
    case cloud_provider::Status::SERVER_ERROR:
    case cloud_provider::Status::NOT_SUPPORTED:
    case cloud_provider::Status::UNKNOWN_ERROR:
      return true;
  }
}

}  // namespace cloud_sync
