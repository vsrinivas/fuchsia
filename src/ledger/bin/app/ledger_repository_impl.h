// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_IMPL_H_
#define SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_IMPL_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/function.h>

#include <set>

#include "src/ledger/bin/app/background_sync_manager.h"
#include "src/ledger/bin/app/db_view_factory.h"
#include "src/ledger/bin/app/disk_cleanup_manager.h"
#include "src/ledger/bin/app/ledger_manager.h"
#include "src/ledger/bin/app/page_eviction_manager.h"
#include "src/ledger/bin/app/page_usage_db.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/app/sync_watcher_set.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/clocks/public/device_id_manager.h"
#include "src/ledger/bin/encryption/impl/encryption_service_factory_impl.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/filesystem/detached_path.h"
#include "src/ledger/bin/p2p_sync/public/user_communicator.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/bin/sync_coordinator/public/user_sync.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/inspect_deprecated/deprecated/expose.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

class LedgerRepositoryImpl : public fuchsia::ledger::internal::LedgerRepositorySyncableDelegate,
                             public PageEvictionManager::Delegate,
                             public BackgroundSyncManager::Delegate,
                             public inspect_deprecated::ChildrenManager {
 public:
  // Creates a new LedgerRepositoryImpl object. Guarantees that |db_factory|
  // will outlive the given |disk_cleanup_manager|.
  LedgerRepositoryImpl(DetachedPath content_path, Environment* environment,
                       std::unique_ptr<storage::DbFactory> db_factory,
                       std::unique_ptr<DbViewFactory> dbview_factory,
                       std::unique_ptr<PageUsageDb> db, std::unique_ptr<SyncWatcherSet> watchers,
                       std::unique_ptr<sync_coordinator::UserSync> user_sync,
                       std::unique_ptr<DiskCleanupManager> disk_cleanup_manager,
                       std::unique_ptr<BackgroundSyncManager> background_sync_manager,
                       std::vector<PageUsageListener*> page_usage_listeners,
                       std::unique_ptr<clocks::DeviceIdManager> device_id_manager,
                       inspect_deprecated::Node inspect_node);
  LedgerRepositoryImpl(const LedgerRepositoryImpl&) = delete;
  LedgerRepositoryImpl& operator=(const LedgerRepositoryImpl&) = delete;
  ~LedgerRepositoryImpl() override;

  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

  void BindRepository(fidl::InterfaceRequest<ledger_internal::LedgerRepository> repository_request);

  // PageEvictionManager::Delegate:
  void PageIsClosedAndSynced(absl::string_view ledger_name, storage::PageIdView page_id,
                             fit::function<void(Status, PagePredicateResult)> callback) override;
  void PageIsClosedOfflineAndEmpty(
      absl::string_view ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PagePredicateResult)> callback) override;
  void DeletePageStorage(absl::string_view ledger_name, storage::PageIdView page_id,
                         fit::function<void(Status)> callback) override;

  // BackgroundSyncManager::Delegate:
  void TrySyncClosedPage(absl::string_view ledger_name, storage::PageIdView page_id) override;

  // LedgerRepository:
  void GetLedger(std::vector<uint8_t> ledger_name, fidl::InterfaceRequest<Ledger> ledger_request,
                 fit::function<void(Status)> callback) override;
  void Duplicate(fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
                 fit::function<void(Status)> callback) override;
  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           fit::function<void(Status)> callback) override;
  void DiskCleanUp(fit::function<void(Status)> callback) override;
  void Close(fit::function<void(Status)> callback) override;

  // inspect_deprecated::ChildrenManager:
  void GetNames(fit::function<void(std::set<std::string>)> callback) override;
  void Attach(std::string ledger_name, fit::function<void(fit::closure)> callback) override;

 private:
  // The internal state of LedgerRepositoryImpl.
  enum class InternalState {
    // The initial state is always |ACTIVE|. Reqests to any of the |LedgerRepository| interface
    // methods can only succeed while on this state.
    ACTIVE,
    // The state is CLOSING when any of the connected clients calls |Close()|.
    CLOSING,
    // The state is CLOSED when this |LedgerRepositoryImpl| is discardable.
    CLOSED,
  };

  // Retrieves the existing, or creates a new LedgerManager object with the
  // given |ledger_name|.
  Status GetLedgerManager(convert::ExtendedStringView ledger_name, LedgerManager** ledger_manager);

  void CheckDiscardable();

  DetachedPath GetPathFor(absl::string_view ledger_name);

  const DetachedPath content_path_;
  Environment* const environment_;

  InternalState state_ = InternalState::ACTIVE;
  callback::AutoCleanableSet<
      SyncableBinding<fuchsia::ledger::internal::LedgerRepositorySyncableDelegate>>
      bindings_;
  std::unique_ptr<storage::DbFactory> db_factory_;
  std::unique_ptr<DbViewFactory> dbview_factory_;
  std::unique_ptr<PageUsageDb> db_;
  encryption::EncryptionServiceFactoryImpl encryption_service_factory_;
  std::unique_ptr<SyncWatcherSet> watchers_;
  std::unique_ptr<sync_coordinator::UserSync> user_sync_;
  std::vector<PageUsageListener*> page_usage_listeners_;
  std::unique_ptr<DiskCleanupManager> disk_cleanup_manager_;
  std::unique_ptr<BackgroundSyncManager> background_sync_manager_;
  // The LedgerManager depends on disk_cleanup_manager_ and background_sync_manager_ in its
  // |page_usage_listeners_|.
  callback::AutoCleanableMap<std::string, LedgerManager, convert::StringViewComparator>
      ledger_managers_;
  fit::closure on_discardable_;

  std::unique_ptr<clocks::DeviceIdManager> device_id_manager_;

  std::vector<fit::function<void(Status)>> cleanup_callbacks_;

  // Callbacks set when closing this repository.
  std::vector<fit::function<void(Status)>> close_callbacks_;

  coroutine::CoroutineManager coroutine_manager_;

  inspect_deprecated::Node inspect_node_;
  inspect_deprecated::UIntMetric requests_metric_;
  inspect_deprecated::Node ledgers_inspect_node_;
  fit::deferred_callback children_manager_retainer_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_LEDGER_REPOSITORY_IMPL_H_
