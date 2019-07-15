// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNC_COORDINATOR_TESTING_FAKE_LEDGER_SYNC_H_
#define SRC_LEDGER_BIN_SYNC_COORDINATOR_TESTING_FAKE_LEDGER_SYNC_H_

#include <lib/fit/function.h>

#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/public/page_sync.h"

namespace sync_coordinator {

// A fake implementation of the LedgerSync.
//
// FakeLedgerSync is responsible for creation of a fake PageSync object and tracking whether the
// corresponding method was called. Stores the information about starts of synchronization for
// pages.
class FakeLedgerSync : public sync_coordinator::LedgerSync {
 public:
  // A fake implementation of the PageSync.
  //
  // Provides a simple implementation of the PageSync methods. All the callbacks are called once
  // the synchronization has been started.
  class FakePageSync;

  FakeLedgerSync();
  FakeLedgerSync(const FakeLedgerSync&) = delete;
  FakeLedgerSync& operator=(const FakeLedgerSync&) = delete;
  ~FakeLedgerSync() override;

  // Returns true if CreatePageSync method was called.
  bool IsCalled();
  // Returns the number of times synchronization was started for the given page.
  int GetSyncCallsCount(const storage::PageId& page_id);

  // LedgerSync:
  std::unique_ptr<sync_coordinator::PageSync> CreatePageSync(
      storage::PageStorage* page_storage, storage::PageSyncClient* page_sync_client) override;

 private:
  // True, if CreatePageSync was called.
  bool called_ = false;
  // Stores a counter per page that records how many times the sync with the cloud was started.
  std::map<storage::PageId, int> sync_page_start_calls_;
};

}  // namespace sync_coordinator

#endif  // SRC_LEDGER_BIN_SYNC_COORDINATOR_TESTING_FAKE_LEDGER_SYNC_H_
