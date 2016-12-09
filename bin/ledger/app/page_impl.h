// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_PAGE_IMPL_H_
#define APPS_LEDGER_SRC_APP_PAGE_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/journal.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace ledger {
class PageManager;
class BranchTracker;

// An implementation of the |Page| interface.
class PageImpl : public Page {
 public:
  PageImpl(storage::PageStorage* storage,
           PageManager* manager,
           BranchTracker* branch_tracker);
  ~PageImpl() override;

 private:
  const storage::CommitId& GetCurrentCommitId();

  void PutInCommit(convert::ExtendedStringView key,
                   storage::ObjectIdView value,
                   storage::KeyPriority priority,
                   std::function<void(Status)> callback);

  // Run |runnable| in a transaction, and notifies |callback| of the result. If
  // a transaction is currently in progress, reuses it, otherwise creates a new
  // one and commit it before calling |callback|.
  void RunInTransaction(
      std::function<Status(storage::Journal* journal)> runnable,
      std::function<void(Status)> callback);

  void CommitJournal(std::unique_ptr<storage::Journal> journal,
                     std::function<void(Status)> callback);

  // Page:
  void GetId(const GetIdCallback& callback) override;

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   const GetSnapshotCallback& callback) override;

  void Watch(fidl::InterfaceHandle<PageWatcher> watcher,
             const WatchCallback& callback) override;

  void Put(fidl::Array<uint8_t> key,
           fidl::Array<uint8_t> value,
           const PutCallback& callback) override;

  void PutWithPriority(fidl::Array<uint8_t> key,
                       fidl::Array<uint8_t> value,
                       Priority priority,
                       const PutWithPriorityCallback& callback) override;

  void PutReference(fidl::Array<uint8_t> key,
                    ReferencePtr reference,
                    Priority priority,
                    const PutReferenceCallback& callback) override;

  void Delete(fidl::Array<uint8_t> key,
              const DeleteCallback& callback) override;

  void CreateReference(int64_t size,
                       mx::socket data,
                       const CreateReferenceCallback& callback) override;

  void GetReference(ReferencePtr reference,
                    const GetReferenceCallback& callback) override;

  void GetPartialReference(
      ReferencePtr reference,
      int64_t offset,
      int64_t max_size,
      const GetPartialReferenceCallback& callback) override;

  void StartTransaction(const StartTransactionCallback& callback) override;

  void Commit(const CommitCallback& callback) override;

  void Rollback(const RollbackCallback& callback) override;

  storage::PageStorage* storage_;
  PageManager* manager_;
  BranchTracker* branch_tracker_;
  storage::CommitId journal_parent_commit_;
  std::unique_ptr<storage::Journal> journal_;
  std::vector<std::unique_ptr<storage::Journal>> in_progress_journals_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_PAGE_IMPL_H_
