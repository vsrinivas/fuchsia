// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_LEDGER_COMMUNICATOR_H_
#define SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_LEDGER_COMMUNICATOR_H_

#include <memory>

#include "src/ledger/bin/p2p_sync/public/page_communicator.h"
#include "src/ledger/bin/storage/public/page_storage.h"

namespace p2p_sync {
// LedgerCommunicator handles ledger-level data transfer between peers.
class LedgerCommunicator {
 public:
  LedgerCommunicator() = default;
  LedgerCommunicator(const LedgerCommunicator&) = delete;
  LedgerCommunicator& operator=(const LedgerCommunicator&) = delete;
  virtual ~LedgerCommunicator() = default;

  // Returns a page-specific communicator.
  // All |PageCommunicator| objects obtained through this method must be
  // destroyed before |LedgerCommunicator|.
  virtual std::unique_ptr<PageCommunicator> GetPageCommunicator(
      storage::PageStorage* storage, storage::PageSyncClient* sync_client) = 0;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_LEDGER_COMMUNICATOR_H_
