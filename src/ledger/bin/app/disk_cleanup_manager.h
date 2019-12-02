// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_DISK_CLEANUP_MANAGER_H_
#define SRC_LEDGER_BIN_APP_DISK_CLEANUP_MANAGER_H_

#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

// Manages cleanup operations in Ledger.
//
// Implementations of DiskCleanupManager define the policies about when and how
// each cleanup operation is executed in Ledger.
class DiskCleanupManager {
 public:
  DiskCleanupManager() = default;
  DiskCleanupManager(const DiskCleanupManager&) = delete;
  DiskCleanupManager& operator=(const DiskCleanupManager&) = delete;
  virtual ~DiskCleanupManager() = default;

  // Sets the callback to be called every time the DiskCleanupManager is empty.
  virtual void SetOnDiscardable(fit::closure on_discardable) = 0;

  // Returns whether the DiskCleanupManager is empty, i.e. whether there are no
  // pending operations.
  virtual bool IsDiscardable() const = 0;

  // Tries to free up disk space.
  virtual void TryCleanUp(fit::function<void(Status)> callback) = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_DISK_CLEANUP_MANAGER_H_
