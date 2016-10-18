// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_JOURNAL_DB_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_JOURNAL_DB_IMPL_H_

#include "apps/ledger/storage/public/journal.h"

#include <memory>
#include <string>

#include "apps/ledger/storage/impl/db.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {

// A |JournalDBImpl| represents a commit in progress.
class JournalDBImpl : public Journal {
 public:
  ~JournalDBImpl() override;

  // Creates a new Journal for a simple commit.
  static std::unique_ptr<Journal> Simple(DB* db,
                                         const JournalId& id,
                                         const CommitId& base);

  // Creates a new Journal for a merge commit.
  static std::unique_ptr<Journal> Merge(DB* db,
                                        const JournalId& id,
                                        const CommitId& base,
                                        const CommitId& other);

  // Returns the id of this journal.
  JournalId GetId() const;

  // Journal :
  Status Put(convert::ExtendedStringView key,
             ObjectIdView object_id,
             KeyPriority priority) override;
  Status Delete(convert::ExtendedStringView key) override;
  Status Commit(CommitId* commit_id) override;
  Status Rollback() override;

 private:
  JournalDBImpl(DB* db, const JournalId& id, const CommitId& base);

  DB* db_;
  const JournalId id_;
  CommitId base_;
  std::unique_ptr<CommitId> other_;
  bool valid_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_JOURNAL_DB_IMPL_H_
