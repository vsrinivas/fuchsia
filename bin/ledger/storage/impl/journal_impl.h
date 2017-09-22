// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_JOURNAL_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_JOURNAL_IMPL_H_

#include "apps/ledger/src/storage/public/journal.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

#include "apps/ledger/src/callback/operation_serializer.h"
#include "apps/ledger/src/coroutine/coroutine.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fxl/macros.h"

namespace storage {

// A |JournalImpl| represents a commit in progress.
class JournalImpl : public Journal {
 public:
  ~JournalImpl() override;

  // Creates a new Journal for a simple commit.
  static std::unique_ptr<Journal> Simple(
      JournalType type,
      coroutine::CoroutineService* coroutine_service,
      PageStorageImpl* page_storage,
      const JournalId& id,
      const CommitId& base);

  // Creates a new Journal for a merge commit.
  static std::unique_ptr<Journal> Merge(
      coroutine::CoroutineService* coroutine_service,
      PageStorageImpl* page_storage,
      const JournalId& id,
      const CommitId& base,
      const CommitId& other);

  // Commits the changes of this |Journal|. Trying to update entries or rollback
  // will fail after a successful commit. The callback will be called with the
  // returned status and the new commit. This Journal object should not be
  // deleted before |callback| is called.
  void Commit(
      std::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  // Rolls back all changes to this |Journal|. Trying to update entries or
  // commit will fail with an |ILLEGAL_STATE| after a successful rollback. This
  // Journal object should not be deleted before |callback| is called.
  void Rollback(std::function<void(Status)> callback);

  // Journal:
  void Put(convert::ExtendedStringView key,
           ObjectIdView object_id,
           KeyPriority priority,
           std::function<void(Status)> callback) override;
  void Delete(convert::ExtendedStringView key,
              std::function<void(Status)> callback) override;
  const JournalId& GetId() const override;

 private:
  JournalImpl(JournalType type,
              coroutine::CoroutineService* coroutine_service,
              PageStorageImpl* page_storage,
              JournalId id,
              CommitId base);

  void GetParents(
      std::function<void(Status,
                         std::vector<std::unique_ptr<const storage::Commit>>)>
          callback);

  void CreateCommitFromChanges(
      std::vector<std::unique_ptr<const storage::Commit>> parents,
      std::unique_ptr<Iterator<const EntryChange>> changes,
      std::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  void GetObjectsToSync(
      std::function<void(Status status, std::vector<ObjectId> objects_to_sync)>
          callback);

  void RollbackInternal(std::function<void(Status)> callback);

  const JournalType type_;
  coroutine::CoroutineService* const coroutine_service_;
  PageStorageImpl* const page_storage_;
  const JournalId id_;
  CommitId base_;
  std::unique_ptr<CommitId> other_;
  // A journal is no longer valid if either commit or rollback have been
  // executed.
  bool valid_;
  // |failed_operation_| is true if any of the Put or Delete methods in this
  // journal have failed. In this case, any operation on EXPLICIT journals
  // other than rolling back will fail. IMPLICIT journals can still be commited
  // even if some operations have failed.
  bool failed_operation_;
  // Serializes all update operations so that entries are inserted in the
  // journal in the order calls to put and delete were received.
  callback::OperationSerializer serializer_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_JOURNAL_IMPL_H_
