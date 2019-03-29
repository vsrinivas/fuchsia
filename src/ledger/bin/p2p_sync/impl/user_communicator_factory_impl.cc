// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/user_communicator_factory_impl.h"

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/netconnector/cpp/fidl.h>
#include <src/lib/fxl/logging.h>

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"

namespace p2p_sync {
UserCommunicatorFactoryImpl::UserCommunicatorFactoryImpl(
    ledger::Environment* environment)
    : environment_(environment) {}

UserCommunicatorFactoryImpl::~UserCommunicatorFactoryImpl() {}

std::unique_ptr<UserCommunicator>
UserCommunicatorFactoryImpl::GetUserCommunicator(
    std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider) {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));
  if (result < 0) {
    FXL_LOG(ERROR) << "unable to get hostname. errno " << errno;
    return nullptr;
  }

  fuchsia::netconnector::NetConnectorPtr net_connector =
      environment_->startup_context()
          ->ConnectToEnvironmentService<fuchsia::netconnector::NetConnector>();

  return std::make_unique<p2p_sync::UserCommunicatorImpl>(
      std::make_unique<p2p_provider::P2PProviderImpl>(
          host_name_buffer, std::move(net_connector),
          std::move(user_id_provider)),
      environment_->coroutine_service());
}

}  // namespace p2p_sync
