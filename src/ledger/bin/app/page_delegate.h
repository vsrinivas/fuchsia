// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_DELEGATE_H_
#define SRC_LEDGER_BIN_APP_PAGE_DELEGATE_H_

#include <lib/callback/operation_serializer.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/function.h>

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "src/ledger/bin/app/branch_tracker.h"
#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/app/page_impl.h"
#include "src/ledger/bin/app/sync_watcher_set.h"
#include "src/ledger/bin/fidl_helpers/bound_interface.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/journal.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {
class PageManager;

// A delegate for the implementation of the |Page| interface.
//
// PageDelegate owns PageImpl and BranchTracker. It makes sure that
// all operations in progress will terminate, even if the Page is no longer
// connected. When the page connection is closed and BranchTracker is also
// empty, the client is notified through |on_empty_callback| (registered by
// |set_on_empty()|).
class PageDelegate {
 public:
  PageDelegate(coroutine::CoroutineService* coroutine_service,
               PageManager* manager, storage::PageStorage* storage,
               MergeResolver* merge_resolver, SyncWatcherSet* watchers,
               std::unique_ptr<PageImpl> page_impl);
  ~PageDelegate();

  void Init(fit::function<void(Status)> on_done);

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

  // From Page interface, called by PageImpl:

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   std::vector<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   fit::function<void(Status)> callback);

  void Put(std::vector<uint8_t> key, std::vector<uint8_t> value,
           fit::function<void(Status)> callback);

  void PutWithPriority(std::vector<uint8_t> key, std::vector<uint8_t> value,
                       Priority priority, fit::function<void(Status)> callback);

  void PutReference(std::vector<uint8_t> key, Reference reference,
                    Priority priority, fit::function<void(Status)> callback);

  void Delete(std::vector<uint8_t> key, fit::function<void(Status)> callback);

  void Clear(fit::function<void(Status)> callback);

  void CreateReference(
      std::unique_ptr<storage::DataSource> data,
      fit::function<void(Status, CreateReferenceStatus, ReferencePtr)>
          callback);

  void StartTransaction(fit::function<void(Status)> callback);

  void Commit(fit::function<void(Status)> callback);

  void Rollback(fit::function<void(Status)> callback);

  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           fit::function<void(Status)> callback);

  void WaitForConflictResolution(
      fit::function<void(Status, ConflictResolutionWaitStatus)> callback);

 private:
  using StatusCallback = fit::function<void(Status)>;

  void PutInCommit(std::vector<uint8_t> key,
                   storage::ObjectIdentifier object_identifier,
                   storage::KeyPriority priority, StatusCallback callback);

  // Runs |runnable| in a transaction, and notifies |callback| of the result. If
  // a transaction is currently in progress, it reuses it, otherwise creates a
  // new one and commits it before calling |callback|. This method is not
  // serialized, and should only be called from a callsite that is serialized.
  void RunInTransaction(fit::function<void(storage::Journal*)> runnable,
                        StatusCallback callback);

  void CommitJournal(
      std::unique_ptr<storage::Journal> journal,
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  void CheckEmpty();

  PageManager* manager_;
  storage::PageStorage* storage_;
  MergeResolver* merge_resolver_;

  BranchTracker branch_tracker_;

  fit::closure on_empty_callback_;

  std::unique_ptr<storage::Journal> journal_;
  callback::OperationSerializer operation_serializer_;
  SyncWatcherSet* watcher_set_;

  std::unique_ptr<PageImpl> page_impl_;

  // This must be the last member of the class.
  fxl::WeakPtrFactory<PageDelegate> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDelegate);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_DELEGATE_H_
