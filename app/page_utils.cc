// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/page_utils.h"

namespace ledger {

Status PageUtils::ConvertStatus(storage::Status status,
                                Status not_found_status) {
  switch (status) {
    case storage::Status::OK:
      return Status::OK;
    case storage::Status::IO_ERROR:
      return Status::IO_ERROR;
    case storage::Status::NOT_FOUND:
      return not_found_status;
    default:
      return Status::INTERNAL_ERROR;
  }
}

}  // namespace ledger
