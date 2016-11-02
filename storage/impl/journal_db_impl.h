// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_JOURNAL_DB_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_JOURNAL_DB_IMPL_H_

#include "apps/ledger/storage/public/journal.h"

#include <memory>
#include <string>

#include "apps/ledger/storage/impl/db.h"
#include "apps/ledger/storage/impl/page_storage_impl.h"
#include "apps/ledger/storage/impl/store/object_store.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {

// A |JournalDBImpl| represents a commit in progress.
class JournalDBImpl : public Journal {
 public:
  ~JournalDBImpl() override;

  // Creates a new Journal for a simple commit.
  static std::unique_ptr<Journal> Simple(JournalType type,
                                         PageStorageImpl* page_storage,
                                         DB* db,
                                         const JournalId& id,
                                         const CommitId& base);

  // Creates a new Journal for a merge commit.
  static std::unique_ptr<Journal> Merge(PageStorageImpl* page_storage,
                                        DB* db,
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
  void Commit(std::function<void(Status, const CommitId&)> callback) override;
  Status Rollback() override;

 private:
  JournalDBImpl(JournalType type,
                PageStorageImpl* page_storage,
                DB* db,
                const JournalId& id,
                const CommitId& base);

  const JournalType type_;
  PageStorageImpl* const page_storage_;
  DB* const db_;
  const JournalId id_;
  CommitId base_;
  ObjectStore object_store_;
  std::unique_ptr<CommitId> other_;
  // A journal is no longer valid if either commit or rollback have been
  // executed.
  bool valid_;
  // |failed_operation_| is true if any of the Put or Delete methods in this
  // journal have failed. In this case, any operation on EXPLICIT journals other
  // than rolling back will fail. IMPLICIT journals can still be commited even
  // if some operations have failed.
  bool failed_operation_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_JOURNAL_DB_IMPL_H_
