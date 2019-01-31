// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_FAKE_DISK_CLEANUP_MANAGER_H_
#define PERIDOT_BIN_LEDGER_TESTING_FAKE_DISK_CLEANUP_MANAGER_H_

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/app/disk_cleanup_manager.h"
#include "peridot/bin/ledger/app/page_usage_listener.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

class FakeDiskCleanupManager : public DiskCleanupManager,
                               public PageUsageListener {
 public:
  FakeDiskCleanupManager() {}
  ~FakeDiskCleanupManager() override {}

  void set_on_empty(fit::closure on_empty_callback) override {}

  bool IsEmpty() override { return true; }

  void TryCleanUp(fit::function<void(Status)> callback) override {
    // Do not call the callback directly.
    cleanup_callback = std::move(callback);
  }
  void OnPageOpened(fxl::StringView /*ledger_name*/,
                    storage::PageIdView /*page_id*/) override {
    ++page_opened_count;
  }

  void OnPageClosed(fxl::StringView /*ledger_name*/,
                    storage::PageIdView /*page_id*/) override {
    ++page_closed_count;
  }

  void OnPageUnused(fxl::StringView /*ledger_name*/,
                    storage::PageIdView /*page_id*/) override {
    ++page_unused_count;
  }

  int page_opened_count = 0;
  int page_closed_count = 0;
  int page_unused_count = 0;
  fit::function<void(Status)> cleanup_callback;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeDiskCleanupManager);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_FAKE_DISK_CLEANUP_MANAGER_H_
