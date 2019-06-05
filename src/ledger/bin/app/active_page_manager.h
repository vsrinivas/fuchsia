// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_ACTIVE_PAGE_MANAGER_H_
#define SRC_LEDGER_BIN_APP_ACTIVE_PAGE_MANAGER_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/callback/scoped_task_runner.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/app/page_delegate.h"
#include "src/ledger/bin/app/page_snapshot_impl.h"
#include "src/ledger/bin/app/sync_watcher_set.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/error_notifier.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl_helpers/bound_interface.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/page_sync_delegate.h"
#include "src/ledger/bin/sync_coordinator/public/page_sync.h"
#include "src/lib/fxl/time/time_delta.h"

namespace ledger {
// Manages a ledger page.
//
// ActivePageManager owns all page-level objects related to a single page: page
// storage, and a set of FIDL PageImpls backed by the page storage. It is safe
// to delete it at any point - this closes all channels, deletes PageImpls and
// tears down the storage.
//
// When the set of PageImpls becomes empty, client is notified through
// |on_empty_callback|.
class ActivePageManager {
 public:
  // Whether the page storage needs to sync with the cloud provider before
  // binding new pages (|NEEDS_SYNC|) or whether it is immediately available
  // (|AVAILABLE|).
  enum class PageStorageState {
    AVAILABLE,
    NEEDS_SYNC,
  };

  // Both |page_storage| and |page_sync| are owned by ActivePageManager and are
  // deleted when it goes away.
  ActivePageManager(Environment* environment,
                    std::unique_ptr<storage::PageStorage> page_storage,
                    std::unique_ptr<sync_coordinator::PageSync> page_sync,
                    std::unique_ptr<MergeResolver> merge_resolver,
                    ActivePageManager::PageStorageState state,
                    zx::duration sync_timeout = zx::sec(5));
  ~ActivePageManager();

  // Creates a new PageDelegate managed by this ActivePageManager, and binds it
  // to the given PageImpl.
  void AddPageImpl(std::unique_ptr<PageImpl> page_impl,
                   fit::function<void(Status)> on_done);

  // Creates a new PageSnapshotImpl managed by this ActivePageManager, and binds
  // it to the request.
  void BindPageSnapshot(std::unique_ptr<const storage::Commit> commit,
                        fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                        std::string key_prefix);

  // Create a new reference for the given object identifier.
  Reference CreateReference(storage::ObjectIdentifier object_identifier);

  // Retrieve an object identifier from a Reference.
  Status ResolveReference(Reference reference,
                          storage::ObjectIdentifier* object_identifier);

  // Checks whether there are any unsynced commits or pieces in this page.
  void IsSynced(fit::function<void(Status, bool)> callback);

  // Checks whether the page is offline and has no entries.
  void IsOfflineAndEmpty(fit::function<void(Status, bool)> callback);

  // Returns true if this ActivePageManager can be deleted without interrupting
  // syncing, merging, or requests related to this page.
  bool IsEmpty();

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

 private:
  void CheckEmpty();
  void OnSyncBacklogDownloaded();

  Environment* const environment_;
  std::unique_ptr<storage::PageStorage> page_storage_;
  std::unique_ptr<sync_coordinator::PageSync> page_sync_;
  std::unique_ptr<MergeResolver> merge_resolver_;
  const zx::duration sync_timeout_;
  callback::AutoCleanableSet<fidl_helpers::BoundInterface<
      PageSnapshot, PageSnapshotImpl,
      ErrorNotifierBinding<fuchsia::ledger::PageSnapshotErrorNotifierDelegate>>>
      snapshots_;
  callback::AutoCleanableSet<PageDelegate> page_delegates_;
  fit::closure on_empty_callback_;

  bool sync_backlog_downloaded_ = false;
  std::vector<std::pair<std::unique_ptr<PageImpl>, fit::function<void(Status)>>>
      page_impls_;

  SyncWatcherSet watchers_;

  // Registered references.
  std::map<uint64_t, storage::ObjectIdentifier> references_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ActivePageManager);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_ACTIVE_PAGE_MANAGER_H_
