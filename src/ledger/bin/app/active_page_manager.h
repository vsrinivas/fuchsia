// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_ACTIVE_PAGE_MANAGER_H_
#define SRC_LEDGER_BIN_APP_ACTIVE_PAGE_MANAGER_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
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
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/fidl_helpers/bound_interface.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/page_sync_delegate.h"
#include "src/ledger/bin/sync_coordinator/public/page_sync.h"
#include "src/ledger/lib/callback/auto_cleanable.h"
#include "src/ledger/lib/callback/scoped_task_runner.h"

namespace ledger {
// Manages an "active" ledger page.
//
// ActivePageManager is responsible for page logic during the portion of the
// page's lifecycle during which FIDL connections to the page are open and a
// storage::PageStorage is instantiated for the page.
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
  ActivePageManager(Environment* environment, std::unique_ptr<storage::PageStorage> page_storage,
                    std::unique_ptr<sync_coordinator::PageSync> page_sync,
                    std::unique_ptr<MergeResolver> merge_resolver,
                    ActivePageManager::PageStorageState state,
                    zx::duration sync_timeout = zx::sec(5));
  ActivePageManager(const ActivePageManager&) = delete;
  ActivePageManager& operator=(const ActivePageManager&) = delete;
  ~ActivePageManager();

  // Creates a new PageDelegate managed by this ActivePageManager, and binds it
  // to the given PageImpl.
  void AddPageImpl(std::unique_ptr<PageImpl> page_impl, fit::function<void(Status)> on_done);

  // Creates a new PageSnapshotImpl managed by this ActivePageManager, and binds
  // it to the request.
  void BindPageSnapshot(std::unique_ptr<const storage::Commit> commit,
                        fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                        std::string key_prefix);

  // Create a new reference for the given object identifier.
  Reference CreateReference(storage::ObjectIdentifier object_identifier);

  // Retrieve an object identifier from a Reference.
  Status ResolveReference(Reference reference, storage::ObjectIdentifier* object_identifier);

  // Checks whether there are any unsynced commits or pieces in this page.
  void IsSynced(fit::function<void(Status, bool)> callback);

  // Checks whether the page is offline and has no entries.
  void IsOfflineAndEmpty(fit::function<void(Status, bool)> callback);

  // Populates |heads| with the page's heads.
  storage::Status GetHeads(std::vector<const storage::CommitId>* heads);

  // Reports to |callback| this page's commits.
  void GetCommits(
      fit::function<void(Status, std::vector<std::unique_ptr<const storage::Commit>>)> callback);

  // Reports to |callback| the |storage::Commit| with the given |storage::CommitId|.
  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=35416): What status is passed to the
  // callback in the commit-was-garbage-collected circumstance?
  void GetCommit(const storage::CommitId& commit_id,
                 fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback);

  // Reports to |on_next| the |storage::Entry|s of the given |storage::Commit| that have a key equal
  // to or greater than |min_key|.
  void GetEntries(const storage::Commit& commit, std::string min_key,
                  fit::function<bool(storage::Entry)> on_next, fit::function<void(Status)> on_done);

  // Reports to |callback| the value associated with |key| in |commit|.
  // TODO(nathaniel): Report more than the first 1024 bytes.
  void GetValue(const storage::Commit& commit, std::string key,
                fit::function<void(Status, std::vector<uint8_t>)> callback);

  // Returns true if this |ActivePageManager| is not currently active in any way and can be deleted.
  bool IsDiscardable() const;

  void SetOnDiscardable(fit::closure on_discardable);

 private:
  void CheckDiscardable();
  void OnSyncBacklogDownloaded();

  Environment* const environment_;
  std::unique_ptr<storage::PageStorage> page_storage_;
  std::unique_ptr<sync_coordinator::PageSync> page_sync_;
  std::unique_ptr<MergeResolver> merge_resolver_;
  const zx::duration sync_timeout_;
  AutoCleanableSet<
      fidl_helpers::BoundInterface<PageSnapshot, PageSnapshotImpl,
                                   SyncableBinding<fuchsia::ledger::PageSnapshotSyncableDelegate>>>
      snapshots_;
  AutoCleanableSet<PageDelegate> page_delegates_;
  fit::closure on_discardable_;

  bool sync_backlog_downloaded_ = false;
  std::vector<std::pair<std::unique_ptr<PageImpl>, fit::function<void(Status)>>> page_impls_;

  SyncWatcherSet watchers_;

  // Registered references.
  std::map<uint64_t, storage::ObjectIdentifier> references_;

  // TODO(nathaniel): This should be upgraded from an integer to a weak_ptr-less-in-this-case
  // TokenManager.
  int64_t ongoing_page_storage_uses_;

  // Must be the last member field.
  ScopedTaskRunner task_runner_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_ACTIVE_PAGE_MANAGER_H_
