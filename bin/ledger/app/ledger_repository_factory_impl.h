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
#include <lib/callback/auto_cleanable.h>
#include <lib/callback/cancellable.h>
#include <lib/callback/managed_container.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/app/disk_cleanup_manager_impl.h"
#include "peridot/bin/ledger/app/ledger_repository_impl.h"
#include "peridot/bin/ledger/cloud_sync/public/user_config.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/error_notifier.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator_factory.h"
#include "peridot/bin/ledger/sync_coordinator/impl/user_sync_impl.h"

namespace ledger {

class LedgerRepositoryFactoryImpl
    : public ::fuchsia::ledger::internal::
          LedgerRepositoryFactoryErrorNotifierDelegate {
 public:
  explicit LedgerRepositoryFactoryImpl(
      Environment* environment,
      std::unique_ptr<p2p_sync::UserCommunicatorFactory>
          user_communicator_factory,
      component::ObjectDir inspect_object_dir);
  ~LedgerRepositoryFactoryImpl() override;

  // LedgerRepositoryFactoryErrorNotifierDelegate:
  void GetRepository(
      zx::channel repository_handle,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      fidl::StringPtr user_id,
      fidl::InterfaceRequest<ledger_internal::LedgerRepository>
          repository_request,
      fit::function<void(Status)> callback) override;

 private:
  class LedgerRepositoryContainer;
  struct RepositoryInformation;

  // Binds |repository_request| to the repository stored in the directory opened
  // in |root_fd|.
  void GetRepositoryByFD(
      fxl::UniqueFD root_fd,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      std::string user_id,
      fidl::InterfaceRequest<ledger_internal::LedgerRepository>
          repository_request,
      fit::function<void(Status)> callback);
  std::unique_ptr<sync_coordinator::UserSyncImpl> CreateUserSync(
      const RepositoryInformation& repository_information,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      SyncWatcherSet* watchers);
  std::unique_ptr<p2p_sync::UserCommunicator> CreateP2PSync(
      const RepositoryInformation& repository_information);
  void OnVersionMismatch(RepositoryInformation repository_information);

  Status DeleteRepositoryDirectory(
      const RepositoryInformation& repository_information);

  Environment* const environment_;
  std::unique_ptr<p2p_sync::UserCommunicatorFactory> const
      user_communicator_factory_;

  callback::AutoCleanableMap<std::string, LedgerRepositoryContainer>
      repositories_;

  component::ObjectDir inspect_object_dir_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
