// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_JOURNAL_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_JOURNAL_IMPL_H_

#include <functional>
#include <map>
#include <memory>
#include <string>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/journal.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// A |JournalImpl| represents an in-memory |Journal|. As such, if the journal's
// type is |IMPLICIT| only one mutation operation (Put/Delete/Clear) can be
// applied before it is commited or rolled back. This way, it is guaranteed that
// in case of unexpected shut down of Ledger, all operations that completed
// successfully (i.e. returned with a status |OK|) will actually be persisted.
// Instances of |JournalImpl| are valid as long as |Commit| has not been called.
// When no longer valid, it is an error to try to call any further methods on
// that object. A journal that is not commited before destruction, will be
// rolled back.
class JournalImpl : public Journal {
 private:
  // Passkey idiom to restrict access to the constructor to static factories.
  class Token;

 public:
  JournalImpl(Token token, JournalType type, ledger::Environment* environment,
              PageStorageImpl* page_storage, CommitId base);
  ~JournalImpl() override;

  // Creates a new Journal for a simple commit.
  static std::unique_ptr<Journal> Simple(JournalType type,
                                         ledger::Environment* environment,
                                         PageStorageImpl* page_storage,
                                         const CommitId& base);

  // Creates a new Journal for a merge commit.
  static std::unique_ptr<Journal> Merge(ledger::Environment* environment,
                                        PageStorageImpl* page_storage,
                                        const CommitId& base,
                                        const CommitId& other);

  // Commits the changes of this |Journal|. Trying to update entries or rollback
  // will fail after a successful commit. The callback will be called with the
  // returned status and the new commit. This Journal object should not be
  // deleted before |callback| is called.
  void Commit(
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  // Journal:
  void Put(convert::ExtendedStringView key, ObjectIdentifier object_identifier,
           KeyPriority priority) override;
  void Delete(convert::ExtendedStringView key) override;
  void Clear() override;

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

  // Creates a new commit. The commit parents are |parents|. The content of the
  // commit is built by executing |changes| over the content pointed by
  // |root_identifier|.
  void CreateCommitFromChanges(
      std::vector<std::unique_ptr<const storage::Commit>> parents,
      ObjectIdentifier root_identifier,
      std::unique_ptr<Iterator<const EntryChange>> changes,
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback);

  void GetObjectsToSync(
      fit::function<void(Status status,
                         std::vector<ObjectIdentifier> objects_to_sync)>
          callback);

  // Returns whether the journal is in a state where it is legal to call |Put|,
  // |Delete| or |Clear|.
  bool StateAllowsMutation();

  const JournalType type_;
  ledger::Environment* const environment_;
  PageStorageImpl* const page_storage_;
  CommitId base_;
  std::unique_ptr<CommitId> other_;

  JournalContainsClearOperation cleared_ = JournalContainsClearOperation::NO;
  std::map<std::string, EntryChange> journal_entries_;

  // A journal is no longer valid after calling commit or rollback.
  bool valid_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_JOURNAL_IMPL_H_
