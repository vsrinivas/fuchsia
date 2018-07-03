// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_FACTORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include <lib/app/cpp/startup_context.h>

#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator_factory.h"

namespace p2p_sync {
// Factory for creating UserCommunicators with default configuration.
class UserCommunicatorFactoryImpl : public UserCommunicatorFactory {
 public:
  UserCommunicatorFactoryImpl(ledger::Environment* environment,
                              fuchsia::sys::StartupContext* startup_context,
                              std::string cobalt_client_name);
  ~UserCommunicatorFactoryImpl() override;

  std::unique_ptr<UserCommunicator> GetUserCommunicator(
      ledger::DetachedPath user_directory) override;

 private:
  ledger::Environment* const environment_;
  fuchsia::sys::StartupContext* const startup_context_;
  const std::string cobalt_client_name_;
};

}  // namespace p2p_sync

#endif  // PERIDOT_BIN_LEDGER_P2P_SYNC_IMPL_USER_COMMUNICATOR_FACTORY_IMPL_H_
