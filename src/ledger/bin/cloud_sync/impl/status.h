// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_STATUS_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_STATUS_H_

#include "src/ledger/bin/fidl/include/types.h"

namespace cloud_sync {

// Returns |true| if a cloud provider error is a permanent error.
bool IsPermanentError(cloud_provider::Status status);

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_STATUS_H_
