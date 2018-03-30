// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_factory.h"

#include <fuchsia/cpp/modular_auth.h>
#include <fuchsia/cpp/netconnector.h>
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"
#include "peridot/bin/ledger/p2p_provider/impl/user_id_provider_impl.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_impl.h"

namespace p2p_sync {
UserCommunicatorFactory::UserCommunicatorFactory(
    ledger::Environment* environment,
    component::ApplicationContext* application_context)
    : environment_(environment), application_context_(application_context) {}

std::unique_ptr<UserCommunicator>
UserCommunicatorFactory::GetDefaultUserCommunicator(
    std::string user_directory) {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));
  if (result < 0) {
    FXL_LOG(ERROR) << "unable to get hostname. errno " << errno;
    return nullptr;
  }

  modular_auth::TokenProviderPtr token_provider =
      application_context_
          ->ConnectToEnvironmentService<modular_auth::TokenProvider>();
  netconnector::NetConnectorPtr net_connector =
      application_context_
          ->ConnectToEnvironmentService<netconnector::NetConnector>();
  std::unique_ptr<p2p_provider::UserIdProviderImpl> user_id_provider =
      std::make_unique<p2p_provider::UserIdProviderImpl>(
          environment_, std::move(user_directory), std::move(token_provider));
  return std::make_unique<p2p_sync::UserCommunicatorImpl>(
      std::make_unique<p2p_provider::P2PProviderImpl>(
          host_name_buffer, std::move(net_connector),
          std::move(user_id_provider)));
}

}  // namespace p2p_sync
