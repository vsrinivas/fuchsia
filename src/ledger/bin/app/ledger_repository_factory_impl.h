// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
#define SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>

#include <memory>
#include <string>

#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/app/ledger_repository_impl.h"
#include "src/ledger/bin/clocks/public/device_fingerprint_manager.h"
#include "src/ledger/bin/cloud_sync/impl/user_sync_impl.h"
#include "src/ledger/bin/cloud_sync/public/user_config.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider_factory.h"
#include "src/ledger/bin/platform/fd.h"
#include "src/ledger/bin/sync_coordinator/impl/user_sync_impl.h"
#include "src/ledger/lib/callback/auto_cleanable.h"
#include "src/ledger/lib/callback/managed_container.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/lib/inspect_deprecated/deprecated/expose.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

class LedgerRepositoryFactoryImpl
    : public ::fuchsia::ledger::internal::LedgerRepositoryFactorySyncableDelegate {
 public:
  explicit LedgerRepositoryFactoryImpl(Environment* environment,
                                       p2p_provider::P2PProviderFactory* p2p_provider_factory,
                                       inspect_deprecated::Node inspect_node);
  LedgerRepositoryFactoryImpl(const LedgerRepositoryFactoryImpl&) = delete;
  LedgerRepositoryFactoryImpl& operator=(const LedgerRepositoryFactoryImpl&) = delete;
  ~LedgerRepositoryFactoryImpl() override;

  // LedgerRepositoryFactorySyncableDelegate:
  void GetRepository(zx::channel repository_handle,
                     fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
                     std::string user_id,
                     fidl::InterfaceRequest<ledger_internal::LedgerRepository> repository_request,
                     fit::function<void(Status)> callback) override;

 private:
  class LedgerRepositoryContainer;
  struct RepositoryInformation;

  // Binds |repository_request| to the repository stored in the directory opened
  // in |root_fd|.
  void GetRepositoryByFD(
      std::shared_ptr<unique_fd> root_fd,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider, std::string user_id,
      fidl::InterfaceRequest<ledger_internal::LedgerRepository> repository_request,
      fit::function<void(Status)> callback);
  Status SynchronousCreateLedgerRepository(
      coroutine::CoroutineHandler* handler,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      RepositoryInformation repository_information,
      std::unique_ptr<LedgerRepositoryImpl>* repository);
  std::unique_ptr<sync_coordinator::UserSyncImpl> CreateUserSync(
      const RepositoryInformation& repository_information,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider, SyncWatcherSet* watchers,
      clocks::DeviceFingerprintManager* fingerprint_manager);
  std::unique_ptr<cloud_sync::UserSyncImpl> CreateCloudSync(
      const RepositoryInformation& repository_information,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      clocks::DeviceFingerprintManager* fingerprint_manager);
  std::unique_ptr<p2p_sync::UserCommunicator> CreateP2PSync(
      const RepositoryInformation& repository_information);
  void OnVersionMismatch(RepositoryInformation repository_information);

  void DeleteRepositoryDirectory(const RepositoryInformation& repository_information);

  Environment* const environment_;
  p2p_provider::P2PProviderFactory* const p2p_provider_factory_;

  AutoCleanableMap<std::string, LedgerRepositoryContainer> repositories_;

  inspect_deprecated::Node inspect_node_;

  coroutine::CoroutineManager coroutine_manager_;

  WeakPtrFactory<LedgerRepositoryFactoryImpl> weak_factory_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
