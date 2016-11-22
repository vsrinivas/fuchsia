// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/test/page_sync_empty_impl.h"

#include "lib/ftl/logging.h"

namespace cloud_sync {
namespace test {

void PageSyncEmptyImpl::Start() {
  FTL_NOTIMPLEMENTED();
}

void PageSyncEmptyImpl::SetOnIdle(ftl::Closure on_idle_callback) {
  FTL_NOTIMPLEMENTED();
}

bool PageSyncEmptyImpl::IsIdle() {
  FTL_NOTIMPLEMENTED();
  return true;
}

void PageSyncEmptyImpl::SetOnBacklogDownloaded(
    ftl::Closure on_backlog_downloaded_callback) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace test
}  // namespace cloud_sync
