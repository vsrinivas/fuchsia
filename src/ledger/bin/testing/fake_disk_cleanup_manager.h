// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_FAKE_DISK_CLEANUP_MANAGER_H_
#define SRC_LEDGER_BIN_TESTING_FAKE_DISK_CLEANUP_MANAGER_H_

#include <lib/fit/function.h>

#include "src/ledger/bin/app/disk_cleanup_manager.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

class FakeDiskCleanupManager : public DiskCleanupManager,
                               public PageUsageListener {
 public:
  FakeDiskCleanupManager() = default;
  ~FakeDiskCleanupManager() override = default;

  void set_on_OnPageUnused(fit::closure on_OnPageUnused_callback) {
    on_OnPageUnused_callback_ = std::move(on_OnPageUnused_callback);
  }

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
    if (on_OnPageUnused_callback_) {
      on_OnPageUnused_callback_();
    }
  }

  int page_opened_count = 0;
  int page_closed_count = 0;
  int page_unused_count = 0;
  fit::closure on_OnPageUnused_callback_;
  fit::function<void(Status)> cleanup_callback;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeDiskCleanupManager);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_FAKE_DISK_CLEANUP_MANAGER_H_
