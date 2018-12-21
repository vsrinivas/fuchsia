// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_IMPL_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/component/cpp/expose.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/function.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/app/disk_cleanup_manager.h"
#include "peridot/bin/ledger/app/ledger_manager.h"
#include "peridot/bin/ledger/app/page_eviction_manager.h"
#include "peridot/bin/ledger/app/sync_watcher_set.h"
#include "peridot/bin/ledger/app/types.h"
#include "peridot/bin/ledger/encryption/impl/encryption_service_factory_impl.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/error_notifier.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator.h"
#include "peridot/bin/ledger/storage/public/db_factory.h"
#include "peridot/bin/ledger/sync_coordinator/public/user_sync.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {

class LedgerRepositoryImpl
    : public fuchsia::ledger::internal::LedgerRepositoryErrorNotifierDelegate,
      public ledger_internal::LedgerRepositoryDebug,
      public PageEvictionManager::Delegate {
 public:
  // Creates a new LedgerRepositoryImpl object. Guarantees that |db_factory|
  // will outlive the given |disk_cleanup_manager|.
  LedgerRepositoryImpl(DetachedPath content_path, Environment* environment,
                       std::unique_ptr<storage::DbFactory> db_factory,
                       std::unique_ptr<SyncWatcherSet> watchers,
                       std::unique_ptr<sync_coordinator::UserSync> user_sync,
                       std::unique_ptr<DiskCleanupManager> disk_cleanup_manager,
                       PageUsageListener* page_usage_listener);
  ~LedgerRepositoryImpl() override;

  // Satisfies an inspection by adding to |out| an object with properties,
  // metrics, and (callbacks affording access to) children of its own.
  void Inspect(std::string display_name,
               component::Object::ObjectVector* out) const;

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
  void GetLedger(fidl::VectorPtr<uint8_t> ledger_name,
                 fidl::InterfaceRequest<Ledger> ledger_request,
                 fit::function<void(Status)> callback) override;
  void Duplicate(
      fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
      fit::function<void(Status)> callback) override;
  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           fit::function<void(Status)> callback) override;
  void GetLedgerRepositoryDebug(
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryDebug> request,
      fit::function<void(Status)> callback) override;
  void DiskCleanUp(fit::function<void(Status)> callback) override;

 private:
  // Retrieves the existing, or creates a new LedgerManager object with the
  // given |ledger_name|.
  Status GetLedgerManager(convert::ExtendedStringView ledger_name,
                          LedgerManager** ledger_manager);

  void CheckEmpty();

  DetachedPath GetPathFor(fxl::StringView ledger_name);

  // LedgerRepositoryDebug:
  void GetInstancesList(GetInstancesListCallback callback) override;

  void GetLedgerDebug(
      fidl::VectorPtr<uint8_t> ledger_name,
      fidl::InterfaceRequest<ledger_internal::LedgerDebug> request,
      GetLedgerDebugCallback callback) override;

  const DetachedPath content_path_;
  Environment* const environment_;
  std::unique_ptr<storage::DbFactory> db_factory_;
  encryption::EncryptionServiceFactoryImpl encryption_service_factory_;
  std::unique_ptr<SyncWatcherSet> watchers_;
  std::unique_ptr<sync_coordinator::UserSync> user_sync_;
  std::unique_ptr<DiskCleanupManager> disk_cleanup_manager_;
  PageUsageListener* page_usage_listener_;
  callback::AutoCleanableMap<std::string, LedgerManager,
                             convert::StringViewComparator>
      ledger_managers_;
  callback::AutoCleanableSet<ErrorNotifierBinding<
      fuchsia::ledger::internal::LedgerRepositoryErrorNotifierDelegate>>
      bindings_;
  fit::closure on_empty_callback_;

  fidl::BindingSet<ledger_internal::LedgerRepositoryDebug>
      ledger_repository_debug_bindings_;

  std::vector<fit::function<void(Status)>> cleanup_callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_IMPL_H_
