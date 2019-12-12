// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_JOURNAL_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_JOURNAL_IMPL_H_

#include <lib/fit/function.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/journal.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine_waiter.h"

namespace storage {

// A |JournalImpl| represents an in-memory |Journal|. As such, if not committed,
// (e.g. because of an unexpected shutdown) its entries will be lost. Instances
// of |JournalImpl| are valid as long as |Commit| has not been called. When no
// longer valid, it is an error to try to call any further methods on that
// object. A journal that is not commited before destruction, will be rolled
// back.
// The parent commits, and the identifiers of their tree roots, are kept alive
// until the journal object is discarded.
class JournalImpl : public Journal {
 private:
  // Passkey idiom to restrict access to the constructor to static factories.
  class Token;

 public:
  JournalImpl(Token token, ledger::Environment* environment, PageStorageImpl* page_storage,
              std::unique_ptr<const Commit> base);
  JournalImpl(const JournalImpl&) = delete;
  JournalImpl& operator=(const JournalImpl&) = delete;
  ~JournalImpl() override;

  // Creates a new Journal for a simple commit.
  static std::unique_ptr<Journal> Simple(ledger::Environment* environment,
                                         PageStorageImpl* page_storage,
                                         std::unique_ptr<const Commit> base);

  // Creates a new Journal for a merge commit.
  static std::unique_ptr<Journal> Merge(ledger::Environment* environment,
                                        PageStorageImpl* page_storage,
                                        std::unique_ptr<const Commit> base,
                                        std::unique_ptr<const Commit> other);

  // Commits the changes of this |Journal|. Trying to update entries or rollback
  // will fail after a successful commit. |commit| will contain:
  // - the new commit if a new commit object has been created.
  // - a null commit if the operation is a no-op.
  // This Journal object should not be deleted during the operation.
  // Note that |commit| is returned but not written to page storage.
  Status Commit(coroutine::CoroutineHandler* handler,
                std::unique_ptr<const storage::Commit>* commit,
                std::vector<ObjectIdentifier>* objects_to_sync);

  // Journal:
  void Put(convert::ExtendedStringView key, ObjectIdentifier object_identifier,
           KeyPriority priority) override;
  void Delete(convert::ExtendedStringView key) override;
  void Clear() override;

 private:
  class Token {
   private:
    Token() = default;
    friend JournalImpl;
  };

  // Creates a new commit. The commit parents are |parents|. The content of the
  // commit is built by executing |changes| over the content pointed by
  // |root_identifier|. |commit| will contain:
  // - the new commit if a new commit object has been created.
  // - a null commit if the operation is a no-op.
  Status CreateCommitFromChanges(coroutine::CoroutineHandler* handler,
                                 std::vector<std::unique_ptr<const storage::Commit>> parents,
                                 btree::LocatedObjectIdentifier root_identifier,
                                 std::vector<EntryChange> changes,
                                 std::unique_ptr<const storage::Commit>* commit,
                                 std::vector<ObjectIdentifier>* objects_to_sync);

  void GetObjectsToSync(
      fit::function<void(Status status, std::vector<ObjectIdentifier> objects_to_sync)> callback);

  // Generate an entry id for newly inserted entries.
  void SetEntryIds(std::vector<EntryChange>* changes);

  void SetEntryIdsSimpleCommit(std::vector<EntryChange>* changes);

  void SetEntryIdsMergeCommit(std::vector<EntryChange>* changes);

  ledger::Environment* const environment_;
  PageStorageImpl* const page_storage_;
  std::unique_ptr<const storage::Commit> base_;
  std::unique_ptr<const storage::Commit> other_;

  JournalContainsClearOperation cleared_ = JournalContainsClearOperation::NO;
  std::map<std::string, EntryChange> journal_entries_;

  // After |Commit| has been called, no further mutations are allowed on the
  // journal.
  bool committed_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_JOURNAL_IMPL_H_
