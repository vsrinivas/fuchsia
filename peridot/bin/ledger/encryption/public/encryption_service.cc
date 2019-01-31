// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/public/encryption_service.h"

namespace encryption {

bool IsPermanentError(Status status) {
  switch (status) {
    case Status::OK:
    case Status::AUTH_ERROR:
    case Status::NETWORK_ERROR:
      return false;
    case Status::INTERNAL_ERROR:
    case Status::INVALID_ARGUMENT:
    case Status::IO_ERROR:
      return true;
  }
}

}  // namespace encryption
