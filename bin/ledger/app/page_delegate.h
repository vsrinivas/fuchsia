// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_DELEGATE_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_DELEGATE_H_

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "lib/callback/operation_serializer.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/ledger/app/branch_tracker.h"
#include "peridot/bin/ledger/app/merging/merge_resolver.h"
#include "peridot/bin/ledger/app/page_impl.h"
#include "peridot/bin/ledger/app/sync_watcher_set.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface.h"
#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/storage/public/journal.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {
class PageManager;

// A delegate for the implementation of the |Page| interface.
//
// PageDelegate owns PageImpl and BranchTracker. It makes sure that all
// operations in progress will terminate, even if the Page is no longer
// connected. When the page connection is closed and BranchTracker is also
// empty, the client is notified through |on_empty_callback| (registered by
// |set_on_empty()|).
class PageDelegate {
 public:
  PageDelegate(coroutine::CoroutineService* coroutine_service,
               PageManager* manager, storage::PageStorage* storage,
               MergeResolver* merge_resolver,
               fidl::InterfaceRequest<Page> request, SyncWatcherSet* watchers);
  ~PageDelegate();

  void Init(std::function<void(Status)> on_done);

  void set_on_empty(fxl::Closure on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

  // From Page interface, called by PageImpl:
  void GetId(Page::GetIdCallback callback);

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   fidl::VectorPtr<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   Page::GetSnapshotCallback callback);

  void Put(fidl::VectorPtr<uint8_t> key, fidl::VectorPtr<uint8_t> value,
           Page::PutCallback callback);

  void PutWithPriority(fidl::VectorPtr<uint8_t> key,
                       fidl::VectorPtr<uint8_t> value, Priority priority,
                       Page::PutWithPriorityCallback callback);

  void PutReference(fidl::VectorPtr<uint8_t> key, Reference reference,
                    Priority priority, Page::PutReferenceCallback callback);

  void Delete(fidl::VectorPtr<uint8_t> key, Page::DeleteCallback callback);

  void CreateReference(std::unique_ptr<storage::DataSource> data,
                       std::function<void(Status, ReferencePtr)> callback);

  void StartTransaction(Page::StartTransactionCallback callback);

  void Commit(Page::CommitCallback callback);

  void Rollback(Page::RollbackCallback callback);

  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           Page::SetSyncStateWatcherCallback callback);

  void WaitForConflictResolution(
      Page::WaitForConflictResolutionCallback callback);

 private:
  using StatusCallback = std::function<void(Status)>;

  const storage::CommitId& GetCurrentCommitId();

  void PutInCommit(fidl::VectorPtr<uint8_t> key,
                   storage::ObjectIdentifier object_identifier,
                   storage::KeyPriority priority, StatusCallback callback);

  // Runs |runnable| in a transaction, and notifies |callback| of the result. If
  // a transaction is currently in progress, it reuses it, otherwise creates a
  // new one and commits it before calling |callback|. This method is not
  // serialized, and should only be called from a callsite that is serialized.
  void RunInTransaction(
      std::function<void(storage::Journal*, std::function<void(Status)>)>
          runnable,
      StatusCallback callback);

  void CommitJournal(
      std::unique_ptr<storage::Journal> journal,
      std::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  void CheckEmpty();

  PageManager* manager_;
  storage::PageStorage* storage_;
  MergeResolver* merge_resolver_;

  fidl::InterfaceRequest<Page> request_;
  fidl_helpers::BoundInterface<Page, PageImpl> interface_;
  BranchTracker branch_tracker_;

  fxl::Closure on_empty_callback_;

  storage::CommitId journal_parent_commit_;
  std::unique_ptr<storage::Journal> journal_;
  callback::OperationSerializer operation_serializer_;
  SyncWatcherSet* watcher_set_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<PageDelegate> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDelegate);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_DELEGATE_H_
