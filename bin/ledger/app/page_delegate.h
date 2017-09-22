// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_PAGE_DELEGATE_H_
#define APPS_LEDGER_SRC_APP_PAGE_DELEGATE_H_

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/app/branch_tracker.h"
#include "peridot/bin/ledger/app/page_impl.h"
#include "peridot/bin/ledger/app/sync_watcher_set.h"
#include "peridot/bin/ledger/callback/operation_serializer.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface.h"
#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/storage/public/journal.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fxl/macros.h"

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
               PageManager* manager,
               storage::PageStorage* storage,
               fidl::InterfaceRequest<Page> request,
               SyncWatcherSet* watchers);
  ~PageDelegate();

  void Init(std::function<void(Status)> on_done);

  void set_on_empty(fxl::Closure on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

  // From Page interface, called by PageImpl:
  void GetId(const Page::GetIdCallback& callback);

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   fidl::Array<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   const Page::GetSnapshotCallback& callback);

  void Put(fidl::Array<uint8_t> key,
           fidl::Array<uint8_t> value,
           const Page::PutCallback& callback);

  void PutWithPriority(fidl::Array<uint8_t> key,
                       fidl::Array<uint8_t> value,
                       Priority priority,
                       const Page::PutWithPriorityCallback& callback);

  void PutReference(fidl::Array<uint8_t> key,
                    ReferencePtr reference,
                    Priority priority,
                    const Page::PutReferenceCallback& callback);

  void Delete(fidl::Array<uint8_t> key, const Page::DeleteCallback& callback);

  void CreateReference(std::unique_ptr<storage::DataSource> data,
                       std::function<void(Status, ReferencePtr)> callback);

  void StartTransaction(const Page::StartTransactionCallback& callback);

  void Commit(const Page::CommitCallback& callback);

  void Rollback(const Page::RollbackCallback& callback);

  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           const Page::SetSyncStateWatcherCallback& callback);

 private:
  using StatusCallback = std::function<void(Status)>;

  const storage::CommitId& GetCurrentCommitId();

  void PutInCommit(fidl::Array<uint8_t> key,
                   storage::ObjectId value,
                   storage::KeyPriority priority,
                   StatusCallback callback);

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

  fidl::InterfaceRequest<Page> request_;
  fidl_helpers::BoundInterface<Page, PageImpl> interface_;
  BranchTracker branch_tracker_;

  fxl::Closure on_empty_callback_;

  storage::CommitId journal_parent_commit_;
  std::unique_ptr<storage::Journal> journal_;
  callback::OperationSerializer operation_serializer_;
  SyncWatcherSet* watcher_set_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDelegate);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_PAGE_DELEGATE_H_
