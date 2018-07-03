// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_H_

#include <memory>

#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/p2p_sync/public/ledger_communicator.h"

namespace p2p_sync {
// UserCommunicator is the user-level object handling peer-to-peer connections
// with the mesh of devices of a user.
class UserCommunicator {
 public:
  UserCommunicator() {}
  virtual ~UserCommunicator() {}

  // Connects this device to its device mesh. To be called exactly once before
  // any other method.
  virtual void Start() = 0;
  // Returns a ledger-specific communicator.
  // All |LedgerCommunicator| objects obtained through this method must be
  // destroyed before |UserCommunicator|.
  virtual std::unique_ptr<LedgerCommunicator> GetLedgerCommunicator(
      std::string repository_id) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(UserCommunicator);
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_H_
