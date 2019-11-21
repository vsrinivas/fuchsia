// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_H_
#define SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_H_

#include <memory>

#include "src/ledger/bin/p2p_sync/public/ledger_communicator.h"

namespace p2p_sync {
// UserCommunicator is the user-level object handling peer-to-peer connections
// with the mesh of devices of a user.
class UserCommunicator {
 public:
  UserCommunicator() = default;
  UserCommunicator(const UserCommunicator&) = delete;
  UserCommunicator& operator=(const UserCommunicator&) = delete;
  virtual ~UserCommunicator() = default;

  // Connects this device to its device mesh. To be called exactly once before
  // any other method.
  virtual void Start() = 0;
  // Returns a ledger-specific communicator.
  // All |LedgerCommunicator| objects obtained through this method must be
  // destroyed before |UserCommunicator|.
  virtual std::unique_ptr<LedgerCommunicator> GetLedgerCommunicator(std::string repository_id) = 0;
};

}  // namespace p2p_sync

#endif  // SRC_LEDGER_BIN_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_H_
