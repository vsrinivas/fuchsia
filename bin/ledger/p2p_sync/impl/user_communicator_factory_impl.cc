// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_factory_impl.h"

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/netconnector/cpp/fidl.h>
#include <lib/fxl/logging.h>

#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"
#include "peridot/bin/ledger/p2p_provider/impl/user_id_provider_impl.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_impl.h"

namespace p2p_sync {
UserCommunicatorFactoryImpl::UserCommunicatorFactoryImpl(
    ledger::Environment* environment,
    component::StartupContext* startup_context, std::string cobalt_client_name)
    : environment_(environment),
      startup_context_(startup_context),
      cobalt_client_name_(std::move(cobalt_client_name)) {}

UserCommunicatorFactoryImpl::~UserCommunicatorFactoryImpl() {}

std::unique_ptr<UserCommunicator>
UserCommunicatorFactoryImpl::GetUserCommunicator(
    ledger::DetachedPath user_directory) {
  char host_name_buffer[HOST_NAME_MAX + 1];
  int result = gethostname(host_name_buffer, sizeof(host_name_buffer));
  if (result < 0) {
    FXL_LOG(ERROR) << "unable to get hostname. errno " << errno;
    return nullptr;
  }

  fuchsia::modular::auth::TokenProviderPtr token_provider =
      startup_context_->ConnectToEnvironmentService<
          fuchsia::modular::auth::TokenProvider>();
  fuchsia::netconnector::NetConnectorPtr net_connector =
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::netconnector::NetConnector>();
  std::unique_ptr<p2p_provider::UserIdProviderImpl> user_id_provider =
      std::make_unique<p2p_provider::UserIdProviderImpl>(
          environment_, startup_context_, std::move(user_directory),
          std::move(token_provider), cobalt_client_name_);
  return std::make_unique<p2p_sync::UserCommunicatorImpl>(
      std::make_unique<p2p_provider::P2PProviderImpl>(
          host_name_buffer, std::move(net_connector),
          std::move(user_id_provider)),
      environment_->coroutine_service());
}

}  // namespace p2p_sync
