// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BASE_COORDINATOR_DELEGATE_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BASE_COORDINATOR_DELEGATE_H_

#include <string>

#include "lib/fxl/functional/closure.h"

namespace cloud_sync {
// Delegate passed to PageDownload and PageUpload to handle coordination and
// access to shared resources.
class BaseCoordinatorDelegate {
 public:
  // Executes |callable| at a later time, subject to exponential backoff.
  // TODO(LE-317): Move this out of the delegate into individual components.
  virtual void Retry(fxl::Closure callable) = 0;

  // Report that the current operation succeeded.
  virtual void Success() = 0;
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_BASE_COORDINATOR_DELEGATE_H_
