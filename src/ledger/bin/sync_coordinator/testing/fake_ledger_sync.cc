// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/sync_coordinator/testing/fake_ledger_sync.h"

#include <lib/fit/function.h>

#include "src/lib/fxl/logging.h"

namespace sync_coordinator {

class FakeLedgerSync::FakePageSync : public PageSync {
 public:
  // PageSync:
  void Start() override {
    started_ = true;

    if (on_backlog_downloaded_) {
      on_backlog_downloaded_();
    }
    if (watcher_) {
      watcher_->Notify({});
    }
    if (on_idle_) {
      on_idle_();
    }
  }

  void SetOnIdle(fit::closure on_idle_callback) override { on_idle_ = std::move(on_idle_callback); }

  bool IsIdle() override { return true; }

  // For this fake, downloads complete immediately, so the on_backlog_downloaded_callback is called
  // right away to avoid waiting for a timeout before fetching the page.
  void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded_callback) override {
    on_backlog_downloaded_ = std::move(on_backlog_downloaded_callback);
  }

  void SetSyncWatcher(SyncStateWatcher* watcher) override {
    watcher_ = watcher;
    if (started_ && watcher_) {
      watcher_->Notify({});
    }
  }

 private:
  fit::closure on_idle_;
  fit::closure on_backlog_downloaded_;
  bool started_ = false;
  sync_coordinator::SyncStateWatcher* watcher_ = nullptr;
};

FakeLedgerSync::FakeLedgerSync() = default;

FakeLedgerSync::~FakeLedgerSync() = default;

bool FakeLedgerSync::IsCalled() { return called_; }

std::unique_ptr<sync_coordinator::PageSync> FakeLedgerSync::CreatePageSync(
    storage::PageStorage* /*page_storage*/, storage::PageSyncClient* /*page_sync_client*/) {
  called_ = true;
  return std::make_unique<FakeLedgerSync::FakePageSync>();
}

}  // namespace sync_coordinator
