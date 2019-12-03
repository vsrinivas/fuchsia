// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_INTEGRATION_SYNC_TEST_SYNC_STATE_WATCHER_H_
#define SRC_LEDGER_BIN_TESTS_INTEGRATION_SYNC_TEST_SYNC_STATE_WATCHER_H_

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "src/ledger/bin/fidl/include/types.h"

namespace ledger {

class TestSyncStateWatcher : public SyncWatcher {
 public:
  TestSyncStateWatcher();
  TestSyncStateWatcher(const TestSyncStateWatcher&) = delete;
  TestSyncStateWatcher& operator=(const TestSyncStateWatcher&) = delete;
  ~TestSyncStateWatcher() override;

  auto NewBinding() { return binding_.NewBinding(); }

  bool Equals(SyncState download, SyncState upload) {
    return download == download_state && upload == upload_state;
  }

  SyncState download_state = SyncState::PENDING;
  SyncState upload_state = SyncState::PENDING;
  int state_change_count = 0;

 private:
  // SyncWatcher:
  void SyncStateChanged(SyncState download, SyncState upload,
                        SyncStateChangedCallback callback) override;

  fidl::Binding<SyncWatcher> binding_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTS_INTEGRATION_SYNC_TEST_SYNC_STATE_WATCHER_H_
