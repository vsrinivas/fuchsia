// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_FACTORY_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_FACTORY_H_

#include "peridot/bin/ledger/p2p_sync/public/user_communicator.h"

#include "peridot/bin/ledger/filesystem/detached_path.h"

namespace p2p_sync {
// Factory for creating UserCommunicators with default configuration.
class UserCommunicatorFactory {
 public:
  UserCommunicatorFactory() {}
  virtual ~UserCommunicatorFactory() {}

  virtual std::unique_ptr<UserCommunicator> GetUserCommunicator(
      ledger::DetachedPath user_directory) = 0;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_PUBLIC_USER_COMMUNICATOR_FACTORY_H_
