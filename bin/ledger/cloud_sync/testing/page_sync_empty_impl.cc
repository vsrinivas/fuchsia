// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/testing/page_sync_empty_impl.h"

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"

namespace cloud_sync {

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

}  // namespace cloud_sync
