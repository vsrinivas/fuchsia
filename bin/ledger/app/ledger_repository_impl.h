// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_IMPL_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/fit/function.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/ledger_manager.h"
#include "peridot/bin/ledger/app/page_eviction_manager.h"
#include "peridot/bin/ledger/app/page_state_reader.h"
#include "peridot/bin/ledger/app/sync_watcher_set.h"
#include "peridot/bin/ledger/app/types.h"
#include "peridot/bin/ledger/encryption/impl/encryption_service_factory_impl.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator.h"
#include "peridot/bin/ledger/sync_coordinator/public/user_sync.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {

class LedgerRepositoryImpl : public ledger_internal::LedgerRepository,
                             public ledger_internal::LedgerRepositoryDebug,
                             public PageStateReader {
 public:
  LedgerRepositoryImpl(
      DetachedPath content_path, Environment* environment,
      std::unique_ptr<SyncWatcherSet> watchers,
      std::unique_ptr<sync_coordinator::UserSync> user_sync,
      std::unique_ptr<PageEvictionManager> page_eviction_manager);
  ~LedgerRepositoryImpl() override;

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

  void BindRepository(fidl::InterfaceRequest<ledger_internal::LedgerRepository>
                          repository_request);

  // Releases all handles bound to this repository impl.
  std::vector<fidl::InterfaceRequest<LedgerRepository>> Unbind();

  // PageStateReader:
  void PageIsClosedAndSynced(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PageClosedAndSynced)> callback) override;

  // LedgerRepository:
  void GetLedger(fidl::VectorPtr<uint8_t> ledger_name,
                 fidl::InterfaceRequest<Ledger> ledger_request,
                 GetLedgerCallback callback) override;
  void Duplicate(
      fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
      DuplicateCallback callback) override;
  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           SetSyncStateWatcherCallback callback) override;
  void GetLedgerRepositoryDebug(
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryDebug> request,
      GetLedgerRepositoryDebugCallback callback) override;
  void DiskCleanUp(DiskCleanUpCallback callback) override;

 private:
  enum CreateIfMissing { YES, NO };

  // Retrieves the existing, or creates a new LedgerManager object with the
  // given |ledger_name|. Returns a pointer to the LedgerManager instance, or
  // nullptr if it is not found and |create_if_missing| is |NO|.
  LedgerManager* GetLedgerManager(convert::ExtendedStringView ledger_name,
                                  CreateIfMissing create_if_missing);

  void CheckEmpty();

  // LedgerRepositoryDebug:
  void GetInstancesList(GetInstancesListCallback callback) override;

  void GetLedgerDebug(
      fidl::VectorPtr<uint8_t> ledger_name,
      fidl::InterfaceRequest<ledger_internal::LedgerDebug> request,
      GetLedgerDebugCallback callback) override;

  const DetachedPath content_path_;
  Environment* const environment_;
  encryption::EncryptionServiceFactoryImpl encryption_service_factory_;
  std::unique_ptr<SyncWatcherSet> watchers_;
  std::unique_ptr<sync_coordinator::UserSync> user_sync_;
  std::unique_ptr<PageEvictionManager> page_eviction_manager_;
  callback::AutoCleanableMap<std::string, LedgerManager,
                             convert::StringViewComparator>
      ledger_managers_;
  fidl::BindingSet<ledger_internal::LedgerRepository> bindings_;
  fit::closure on_empty_callback_;

  fidl::BindingSet<ledger_internal::LedgerRepositoryDebug>
      ledger_repository_debug_bindings_;

  bool clean_up_in_progress_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_IMPL_H_
