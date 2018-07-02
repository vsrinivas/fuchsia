// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/netconnector/cpp/fidl.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/callback/cancellable.h"
#include "lib/callback/managed_container.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/ledger_repository_impl.h"
#include "peridot/bin/ledger/app/page_eviction_manager_impl.h"
#include "peridot/bin/ledger/cloud_sync/public/user_config.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator_factory.h"

namespace ledger {

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
  void GetRepositoryDeprecated(
      fidl::StringPtr repository_path,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      fidl::InterfaceRequest<ledger_internal::LedgerRepository>
          repository_request,
      GetRepositoryDeprecatedCallback callback) override;
  void GetRepository(
      zx::channel repository_handle,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      fidl::InterfaceRequest<ledger_internal::LedgerRepository>
          repository_request,
      GetRepositoryCallback callback) override;

  // Binds |repository_request| to the repository stored in the directory opened
  // in |root_fd|.
  void GetRepositoryByFD(
      fxl::UniqueFD root_fd,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      fidl::InterfaceRequest<ledger_internal::LedgerRepository>
          repository_request,
      GetRepositoryCallback callback);
  void CreateRepository(
      LedgerRepositoryContainer* container,
      const RepositoryInformation& repository_information,
      cloud_sync::UserConfig user_config,
      std::unique_ptr<PageEvictionManagerImpl> page_eviction_manager);
  std::unique_ptr<p2p_sync::UserCommunicator> CreateP2PSync(
      const RepositoryInformation& repository_information);
  void OnVersionMismatch(const RepositoryInformation& repository_information);

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
