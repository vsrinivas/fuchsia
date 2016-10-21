// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_PAGE_IMPL_H_
#define APPS_LEDGER_APP_PAGE_IMPL_H_

#include <memory>
#include <string>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/public/journal.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"

namespace ledger {

// An implementation of the |Page| interface.
class PageImpl : public Page {
 public:
  PageImpl(storage::PageStorage* storage);
  ~PageImpl() override;

 private:
  // Returns the current page head commit. If the page has multiple head
  // commits, returns the head ahead of the last locally created or presented
  // commit. If multiple heads match this criteria, returns one arbitrarily.
  storage::CommitId GetLocalBranchHeadCommit();

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

  void GetSnapshot(const GetSnapshotCallback& callback) override;

  void Watch(mojo::InterfaceHandle<PageWatcher> watcher,
             const WatchCallback& callback) override;

  void Put(mojo::Array<uint8_t> key,
           mojo::Array<uint8_t> value,
           const PutCallback& callback) override;

  void PutWithPriority(mojo::Array<uint8_t> key,
                       mojo::Array<uint8_t> value,
                       Priority priority,
                       const PutWithPriorityCallback& callback) override;

  void PutReference(mojo::Array<uint8_t> key,
                    ReferencePtr reference,
                    Priority priority,
                    const PutReferenceCallback& callback) override;

  void Delete(mojo::Array<uint8_t> key,
              const DeleteCallback& callback) override;

  void CreateReference(int64_t size,
                       mojo::ScopedDataPipeConsumerHandle data,
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

  void GetReferenceInternal(
      ReferencePtr reference,
      std::function<void(Status, ftl::StringView)> callback);

  storage::PageStorage* storage_;
  storage::CommitId journal_parent_commit_;
  std::unique_ptr<storage::Journal> journal_;
  std::vector<std::unique_ptr<storage::Journal>> in_progress_journals_;
  mojo::StrongBindingSet<PageSnapshot> page_snapshot_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_APP_PAGE_IMPL_H_
