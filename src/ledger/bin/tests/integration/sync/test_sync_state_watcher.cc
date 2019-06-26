// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/integration/sync/test_sync_state_watcher.h"

namespace ledger {

TestSyncStateWatcher::TestSyncStateWatcher() : binding_(this) {}
TestSyncStateWatcher::~TestSyncStateWatcher() {}

void TestSyncStateWatcher::SyncStateChanged(SyncState download, SyncState upload,
                                            SyncStateChangedCallback callback) {
  state_change_count++;
  download_state = download;
  upload_state = upload;
  callback();
}

}  // namespace ledger
