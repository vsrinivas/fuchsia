// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_IMPL_H_
#define SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_IMPL_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/function.h>
#include <lib/inspect/deprecated/expose.h>
#include <lib/inspect/inspect.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/disk_cleanup_manager.h"
#include "src/ledger/bin/app/ledger_manager.h"
#include "src/ledger/bin/app/page_eviction_manager.h"
#include "src/ledger/bin/app/sync_watcher_set.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/encryption/impl/encryption_service_factory_impl.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/error_notifier.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/filesystem/detached_path.h"
#include "src/ledger/bin/p2p_sync/public/user_communicator.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/sync_coordinator/public/user_sync.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace ledger {

class LedgerRepositoryImpl
    : public fuchsia::ledger::internal::LedgerRepositoryErrorNotifierDelegate,
      public PageEvictionManager::Delegate {
 public:
  // Creates a new LedgerRepositoryImpl object. Guarantees that |db_factory|
  // will outlive the given |disk_cleanup_manager|.
  LedgerRepositoryImpl(DetachedPath content_path, Environment* environment,
                       std::unique_ptr<storage::DbFactory> db_factory,
                       std::unique_ptr<SyncWatcherSet> watchers,
                       std::unique_ptr<sync_coordinator::UserSync> user_sync,
                       std::unique_ptr<DiskCleanupManager> disk_cleanup_manager,
                       PageUsageListener* page_usage_listener,
                       inspect::Node inspect_node);
  ~LedgerRepositoryImpl() override;

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

  void BindRepository(fidl::InterfaceRequest<ledger_internal::LedgerRepository>
                          repository_request);

  // Releases all handles bound to this repository impl.
  std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
  Unbind();

  // PageEvictionManager::Delegate:
  void PageIsClosedAndSynced(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PagePredicateResult)> callback) override;
  void PageIsClosedOfflineAndEmpty(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PagePredicateResult)> callback) override;
  void DeletePageStorage(fxl::StringView ledger_name,
                         storage::PageIdView page_id,
                         fit::function<void(Status)> callback) override;

  // LedgerRepository:
  void GetLedger(std::vector<uint8_t> ledger_name,
                 fidl::InterfaceRequest<Ledger> ledger_request,
                 fit::function<void(Status)> callback) override;
  void Duplicate(
      fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
      fit::function<void(Status)> callback) override;
  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           fit::function<void(Status)> callback) override;
  void DiskCleanUp(fit::function<void(Status)> callback) override;

 private:
  // Retrieves the existing, or creates a new LedgerManager object with the
  // given |ledger_name|.
  Status GetLedgerManager(convert::ExtendedStringView ledger_name,
                          LedgerManager** ledger_manager);

  void CheckEmpty();

  DetachedPath GetPathFor(fxl::StringView ledger_name);

  const DetachedPath content_path_;
  Environment* const environment_;
  std::unique_ptr<storage::DbFactory> db_factory_;
  encryption::EncryptionServiceFactoryImpl encryption_service_factory_;
  std::unique_ptr<SyncWatcherSet> watchers_;
  std::unique_ptr<sync_coordinator::UserSync> user_sync_;
  PageUsageListener* page_usage_listener_;
  callback::AutoCleanableMap<std::string, LedgerManager,
                             convert::StringViewComparator>
      ledger_managers_;
  // The DiskCleanupManager relies on the |ledger_managers_| being still alive.
  std::unique_ptr<DiskCleanupManager> disk_cleanup_manager_;
  callback::AutoCleanableSet<ErrorNotifierBinding<
      fuchsia::ledger::internal::LedgerRepositoryErrorNotifierDelegate>>
      bindings_;
  fit::closure on_empty_callback_;

  std::vector<fit::function<void(Status)>> cleanup_callbacks_;

  inspect::Node inspect_node_;
  inspect::UIntMetric requests_metric_;
  inspect::Node ledgers_inspect_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImpl);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_IMPL_H_
