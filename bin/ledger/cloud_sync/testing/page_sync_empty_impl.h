// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_TESTING_PAGE_SYNC_EMPTY_IMPL_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_TESTING_PAGE_SYNC_EMPTY_IMPL_H_

#include "lib/fxl/functional/closure.h"
#include "peridot/bin/ledger/cloud_sync/public/page_sync.h"

namespace cloud_sync {

class PageSyncEmptyImpl : public PageSync {
 public:
  // PageSync:
  void Start() override;
  void SetOnIdle(fxl::Closure on_idle_callback) override;
  bool IsIdle() override;
  void SetOnBacklogDownloaded(
      fxl::Closure on_backlog_downloaded_callback) override;
  void SetSyncWatcher(SyncStateWatcher* watcher) override;
};

}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_TESTING_PAGE_SYNC_EMPTY_IMPL_H_
