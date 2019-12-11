// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_sync/impl/user_communicator_factory_impl.h"

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/overnet/cpp/fidl.h>

#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"

namespace p2p_sync {
UserCommunicatorFactoryImpl::UserCommunicatorFactoryImpl(ledger::Environment* environment)
    : environment_(environment) {}

UserCommunicatorFactoryImpl::~UserCommunicatorFactoryImpl() = default;

std::unique_ptr<UserCommunicator> UserCommunicatorFactoryImpl::GetUserCommunicator(
    std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider) {
  fuchsia::overnet::OvernetPtr overnet =
      environment_->component_context()->svc()->Connect<fuchsia::overnet::Overnet>();

  return std::make_unique<p2p_sync::UserCommunicatorImpl>(
      environment_, std::make_unique<p2p_provider::P2PProviderImpl>(
                        std::move(overnet), std::move(user_id_provider), environment_->random()));
}

}  // namespace p2p_sync
