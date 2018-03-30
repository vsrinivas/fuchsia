// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_FACTORY_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_FACTORY_H_

#include <memory>
#include "lib/app/cpp/application_context.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator.h"

namespace p2p_sync {
// Factory for creating UserCommunicators with default configuration.
class UserCommunicatorFactory {
 public:
  UserCommunicatorFactory(ledger::Environment* environment,
                          component::ApplicationContext* application_context);

  std::unique_ptr<UserCommunicator> GetDefaultUserCommunicator(
      std::string user_directory);

 private:
  ledger::Environment* const environment_;
  component::ApplicationContext* const application_context_;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_FACTORY_H_
