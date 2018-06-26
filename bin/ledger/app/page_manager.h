// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_MANAGER_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_MANAGER_H_

#include <memory>
#include <vector>

#include <fuchsia/ledger/internal/cpp/fidl.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/callback/scoped_task_runner.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/time/time_delta.h"
#include "peridot/bin/ledger/app/merging/merge_resolver.h"
#include "peridot/bin/ledger/app/page_delegate.h"
#include "peridot/bin/ledger/app/page_snapshot_impl.h"
#include "peridot/bin/ledger/app/sync_watcher_set.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/page_sync_delegate.h"
#include "peridot/bin/ledger/sync_coordinator/public/page_sync.h"

namespace ledger {
// Manages a ledger page.
//
// PageManager owns all page-level objects related to a single page: page
// storage, and a set of FIDL PageImpls backed by the page storage. It is safe
// to delete it at any point - this closes all channels, deletes PageImpls and
// tears down the storage.
//
// When the set of PageImpls becomes empty, client is notified through
// |on_empty_callback|.
class PageManager : public ledger_internal::PageDebug {
 public:
  // Whether the page storage needs to sync with the cloud provider before
  // binding new pages (|NEEDS_SYNC|) or whether it is immediately available
  // (|AVAILABLE|).
  enum class PageStorageState {
    AVAILABLE,
    NEEDS_SYNC,
  };

  // Both |page_storage| and |page_sync| are owned by PageManager and are
  // deleted when it goes away.
  PageManager(Environment* environment,
              std::unique_ptr<storage::PageStorage> page_storage,
              std::unique_ptr<sync_coordinator::PageSync> page_sync,
              std::unique_ptr<MergeResolver> merge_resolver,
              PageManager::PageStorageState state,
              zx::duration sync_timeout = zx::sec(5));
  ~PageManager() override;

  // Creates a new PageImpl managed by this PageManager, and binds it to the
  // request.
  void BindPage(fidl::InterfaceRequest<Page> page_request,
                std::function<void(Status)> on_done);

  // Binds |page_debug| request and fires |callback| with Status::OK.
  void BindPageDebug(fidl::InterfaceRequest<PageDebug> page_debug,
                     std::function<void(Status)> callback);

  // Creates a new PageSnapshotImpl managed by this PageManager, and binds it to
  // the request.
  void BindPageSnapshot(std::unique_ptr<const storage::Commit> commit,
                        fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                        std::string key_prefix);

  // Create a new reference for the given object identifier.
  Reference CreateReference(storage::ObjectIdentifier object_identifier);

  // Retrieve an object identifier from a Reference.
  Status ResolveReference(Reference reference,
                          storage::ObjectIdentifier* object_identifier);

  void set_on_empty(const fxl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  void CheckEmpty();
  void OnSyncBacklogDownloaded();

  void GetHeadCommitsIds(GetHeadCommitsIdsCallback callback) override;

  void GetSnapshot(ledger_internal::CommitId commit_id,
                   fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   GetSnapshotCallback callback) override;

  void GetCommit(ledger_internal::CommitId commit_id,
                 GetCommitCallback callback) override;

  Environment* const environment_;
  std::unique_ptr<storage::PageStorage> page_storage_;
  std::unique_ptr<sync_coordinator::PageSync> page_sync_;
  std::unique_ptr<MergeResolver> merge_resolver_;
  const zx::duration sync_timeout_;
  callback::AutoCleanableSet<
      fidl_helpers::BoundInterface<PageSnapshot, PageSnapshotImpl>>
      snapshots_;
  callback::AutoCleanableSet<PageDelegate> pages_;
  fxl::Closure on_empty_callback_;

  bool sync_backlog_downloaded_ = false;
  std::vector<
      std::pair<fidl::InterfaceRequest<Page>, std::function<void(Status)>>>
      page_requests_;

  SyncWatcherSet watchers_;

  fidl::BindingSet<PageDebug> page_debug_bindings_;

  // Registered references.
  std::map<uint64_t, storage::ObjectIdentifier> references_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageManager);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_MANAGER_H_
