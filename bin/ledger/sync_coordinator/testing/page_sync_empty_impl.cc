// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/sync_coordinator/testing/page_sync_empty_impl.h"

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"

namespace sync_coordinator {

void PageSyncEmptyImpl::Start() { FXL_NOTIMPLEMENTED(); }

void PageSyncEmptyImpl::SetOnIdle(fxl::Closure /*on_idle_callback*/) {
  FXL_NOTIMPLEMENTED();
}

bool PageSyncEmptyImpl::IsIdle() {
  FXL_NOTIMPLEMENTED();
  return true;
}

void PageSyncEmptyImpl::SetOnBacklogDownloaded(
    fxl::Closure /*on_backlog_downloaded_callback*/) {
  FXL_NOTIMPLEMENTED();
}

void PageSyncEmptyImpl::SetSyncWatcher(SyncStateWatcher* /*watcher*/) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace sync_coordinator
