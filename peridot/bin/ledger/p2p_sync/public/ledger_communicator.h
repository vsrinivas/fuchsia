// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_LEDGER_COMMUNICATOR_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_LEDGER_COMMUNICATOR_H_

#include <memory>

#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/p2p_sync/public/page_communicator.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace p2p_sync {
// LedgerCommunicator handles ledger-level data transfer between peers.
class LedgerCommunicator {
 public:
  LedgerCommunicator() {}
  virtual ~LedgerCommunicator() {}

  // Returns a page-specific communicator.
  // All |PageCommunicator| objects obtained through this method must be
  // destroyed before |LedgerCommunicator|.
  virtual std::unique_ptr<PageCommunicator> GetPageCommunicator(
      storage::PageStorage* storage, storage::PageSyncClient* sync_client) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerCommunicator);
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_LEDGER_COMMUNICATOR_H_
