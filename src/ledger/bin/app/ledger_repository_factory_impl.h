// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
#define SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/netconnector/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/callback/cancellable.h>
#include <lib/callback/managed_container.h>
#include <lib/inspect/deprecated/expose.h>
#include <lib/inspect/inspect.h>

#include <memory>
#include <string>

#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/app/ledger_repository_impl.h"
#include "src/ledger/bin/cloud_sync/public/user_config.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/error_notifier.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/p2p_sync/public/user_communicator_factory.h"
#include "src/ledger/bin/sync_coordinator/impl/user_sync_impl.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace ledger {

class LedgerRepositoryFactoryImpl
    : public ::fuchsia::ledger::internal::
          LedgerRepositoryFactoryErrorNotifierDelegate {
 public:
  explicit LedgerRepositoryFactoryImpl(
      Environment* environment,
      std::unique_ptr<p2p_sync::UserCommunicatorFactory>
          user_communicator_factory,
      inspect::Object inspect_object);
  ~LedgerRepositoryFactoryImpl() override;

  // LedgerRepositoryFactoryErrorNotifierDelegate:
  void GetRepository(
      zx::channel repository_handle,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      std::string user_id,
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
      fit::function<void(storage::Status)> callback);
  std::unique_ptr<sync_coordinator::UserSyncImpl> CreateUserSync(
      const RepositoryInformation& repository_information,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      SyncWatcherSet* watchers);
  std::unique_ptr<p2p_sync::UserCommunicator> CreateP2PSync(
      const RepositoryInformation& repository_information);
  void OnVersionMismatch(RepositoryInformation repository_information);

  storage::Status DeleteRepositoryDirectory(
      const RepositoryInformation& repository_information);

  Environment* const environment_;
  std::unique_ptr<p2p_sync::UserCommunicatorFactory> const
      user_communicator_factory_;

  callback::AutoCleanableMap<std::string, LedgerRepositoryContainer>
      repositories_;

  inspect::Object inspect_object_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImpl);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
