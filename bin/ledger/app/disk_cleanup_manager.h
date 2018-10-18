// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_DISK_CLEANUP_MANAGER_H_
#define PERIDOT_BIN_LEDGER_APP_DISK_CLEANUP_MANAGER_H_

#include <lib/fxl/functional/closure.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/app/page_usage_listener.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

// Manages cleanup operations in Ledger.
//
// Implementations of DiskCleanupManager define the policies about when and how
// each cleanup operation is executed in Ledger.
class DiskCleanupManager : public PageUsageListener {
 public:
  DiskCleanupManager() {}
  virtual ~DiskCleanupManager() override {}

  // Sets the callback to be called every time the DiskCleanupManager is empty.
  virtual void set_on_empty(fit::closure on_empty_callback) = 0;

  // Returns whether the DiskCleanupManager is empty, i.e. whether there are no
  // pending operations.
  virtual bool IsEmpty() = 0;

  // Tries to free up disk space.
  virtual void TryCleanUp(fit::function<void(Status)> callback) = 0;

  // PageUsageListener:
  void OnPageOpened(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override = 0;
  void OnPageClosed(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override = 0;
  void OnPageUnused(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DiskCleanupManager);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_DISK_CLEANUP_MANAGER_H_
