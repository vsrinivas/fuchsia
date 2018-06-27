// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_TESTING_PAGE_SYNC_EMPTY_IMPL_H_
#define PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_TESTING_PAGE_SYNC_EMPTY_IMPL_H_

#include <lib/fit/function.h>

#include "peridot/bin/ledger/sync_coordinator/public/page_sync.h"

namespace sync_coordinator {

class PageSyncEmptyImpl : public PageSync {
 public:
  // PageSync:
  void Start() override;
  void SetOnIdle(fit::closure on_idle_callback) override;
  bool IsIdle() override;
  void SetOnBacklogDownloaded(
      fit::closure on_backlog_downloaded_callback) override;
  void SetSyncWatcher(SyncStateWatcher* watcher) override;
};

}  // namespace sync_coordinator

#endif  // PERIDOT_BIN_LEDGER_SYNC_COORDINATOR_TESTING_PAGE_SYNC_EMPTY_IMPL_H_
