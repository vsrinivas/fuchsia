// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_JOURNAL_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_JOURNAL_IMPL_H_

#include <functional>
#include <memory>
#include <string>

#include <lib/callback/operation_serializer.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/journal.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// A |JournalImpl| represents a commit in progress.
class JournalImpl : public Journal {
 private:
  // Passkey idiom to restrict access to the constructor to static factories.
  class Token;

 public:
  JournalImpl(Token token, JournalType type,
              coroutine::CoroutineService* coroutine_service,
              PageStorageImpl* page_storage, JournalId id, CommitId base);
  ~JournalImpl() override;

  // Creates a new Journal for a simple commit.
  static std::unique_ptr<Journal> Simple(
      JournalType type, coroutine::CoroutineService* coroutine_service,
      PageStorageImpl* page_storage, const JournalId& id, const CommitId& base);

  // Creates a new Journal for a merge commit.
  static std::unique_ptr<Journal> Merge(
      coroutine::CoroutineService* coroutine_service,
      PageStorageImpl* page_storage, const JournalId& id, const CommitId& base,
      const CommitId& other);

  // Commits the changes of this |Journal|. Trying to update entries or rollback
  // will fail after a successful commit. The callback will be called with the
  // returned status and the new commit. This Journal object should not be
  // deleted before |callback| is called.
  void Commit(
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  // Rolls back all changes to this |Journal|. Trying to update entries or
  // commit will fail with an |ILLEGAL_STATE| after a successful rollback. This
  // Journal object should not be deleted before |callback| is called.
  void Rollback(fit::function<void(Status)> callback);

  // Journal:
  void Put(convert::ExtendedStringView key, ObjectIdentifier object_identifier,
           KeyPriority priority, fit::function<void(Status)> callback) override;
  void Delete(convert::ExtendedStringView key,
              fit::function<void(Status)> callback) override;
  const JournalId& GetId() const override;

 private:
  class Token {
   private:
    Token() {}
    friend JournalImpl;
  };

  void GetParents(
      fit::function<void(Status,
                         std::vector<std::unique_ptr<const storage::Commit>>)>
          callback);

  void CreateCommitFromChanges(
      std::vector<std::unique_ptr<const storage::Commit>> parents,
      std::unique_ptr<Iterator<const EntryChange>> changes,
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  void GetObjectsToSync(
      fit::function<void(Status status,
                         std::vector<ObjectIdentifier> objects_to_sync)>
          callback);

  void RollbackInternal(fit::function<void(Status)> callback);

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

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_JOURNAL_IMPL_H_
