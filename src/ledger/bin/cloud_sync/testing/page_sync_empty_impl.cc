// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/testing/page_sync_empty_impl.h"

#include <lib/fit/function.h>

#include "src/ledger/lib/logging/logging.h"

namespace cloud_sync {

void PageSyncEmptyImpl::Start() { LEDGER_NOTIMPLEMENTED(); }

void PageSyncEmptyImpl::SetOnPaused(fit::closure /*on_paused_callback*/) {
  LEDGER_NOTIMPLEMENTED();
}

bool PageSyncEmptyImpl::IsPaused() {
  LEDGER_NOTIMPLEMENTED();
  return true;
}

void PageSyncEmptyImpl::SetOnBacklogDownloaded(fit::closure /*on_backlog_downloaded_callback*/) {
  LEDGER_NOTIMPLEMENTED();
}

void PageSyncEmptyImpl::SetSyncWatcher(SyncStateWatcher* /*watcher*/) { LEDGER_NOTIMPLEMENTED(); }

void PageSyncEmptyImpl::SetOnUnrecoverableError(fit::closure /*on_error*/) {
  LEDGER_NOTIMPLEMENTED();
}

}  // namespace cloud_sync
