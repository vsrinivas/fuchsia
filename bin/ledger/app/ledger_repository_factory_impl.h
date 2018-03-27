// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include <fuchsia/cpp/cloud_provider.h>
#include <fuchsia/cpp/ledger_internal.h>
#include <fuchsia/cpp/modular_auth.h>
#include <fuchsia/cpp/netconnector.h>
#include "garnet/lib/callback/auto_cleanable.h"
#include "garnet/lib/callback/cancellable.h"
#include "garnet/lib/callback/managed_container.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/ledger_repository_impl.h"
#include "peridot/bin/ledger/cloud_sync/public/user_config.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_factory.h"

namespace ledger {

using NetConnectorFactory = std::function<netconnector::NetConnectorPtr()>;
using TokenProviderFactory = std::function<modular_auth::TokenProviderPtr()>;

class LedgerRepositoryFactoryImpl
    : public ledger_internal::LedgerRepositoryFactory {
 public:
  explicit LedgerRepositoryFactoryImpl(
      ledger::Environment* environment,
      std::unique_ptr<p2p_sync::UserCommunicatorFactory>
          user_communicator_factory);
  ~LedgerRepositoryFactoryImpl() override;

 private:
  class LedgerRepositoryContainer;
  struct RepositoryInformation;

  // LedgerRepositoryFactory:
  void GetRepository(
      fidl::StringPtr repository_path,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      fidl::InterfaceRequest<ledger_internal::LedgerRepository>
          repository_request,
      GetRepositoryCallback callback) override;

  void CreateRepository(LedgerRepositoryContainer* container,
                        const RepositoryInformation& repository_information,
                        cloud_sync::UserConfig user_config);
  std::unique_ptr<p2p_sync::UserCommunicator> CreateP2PSync(
      const RepositoryInformation& repository_information);
  void OnVersionMismatch(RepositoryInformation repository_information);

  Status DeleteRepositoryDirectory(
      const RepositoryInformation& repository_information);

  ledger::Environment* const environment_;
  std::unique_ptr<p2p_sync::UserCommunicatorFactory> const
      user_communicator_factory_;

  callback::AutoCleanableMap<std::string, LedgerRepositoryContainer>
      repositories_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
