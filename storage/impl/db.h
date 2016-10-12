// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_DB_H_
#define APPS_LEDGER_STORAGE_IMPL_DB_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/storage/public/iterator.h"
#include "apps/ledger/storage/public/journal.h"
#include "apps/ledger/storage/public/types.h"
#include "leveldb/db.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {

// |DB| manages all Ledger related data that are stored in LevelDB. This
// includes commit objects, information on head commits, as well as metadata on
// on which objects and commits are not yet synchronized to the cloud.
class DB {
 public:
  DB(std::string db_path);
  ~DB();

  // Initializes LevelDB or returns an |IO_ERROR| on failure.
  Status Init();

  // Finds all head commits and replaces the contents of |heads| with their ids.
  // Returns |OK| on success or |IO_ERROR| in case of an error reading the
  // values. It is not an error if no heads are found.
  Status GetHeads(std::vector<CommitId>* heads);
  // Adds the given |head| in the set of commit heads.
  Status AddHead(const CommitId& head);
  // Removes the given |head| from the head commits.
  Status RemoveHead(const CommitId& head);
  // Returns |OK| if the commit with the given |commit_id| is head commits or
  // |NOT_FOUND| if not.
  Status ContainsHead(const CommitId& commit_id);

  // Finds the commit with the given |commit_id| and stores its represenation in
  // storage bytes in the |storage_bytes| string.
  Status GetCommitStorageBytes(const CommitId& commit_id,
                               std::string* storage_bytes);
  // Adds the given |commit| in the database.
  Status AddCommitStorageBytes(const CommitId& commit_id,
                               const std::string& storage_bytes);
  // Removes the commit with the given |commit_id| from the commits.
  Status RemoveCommit(const CommitId& commit_id);

  // Creates a new |Journal| with the given |base| commit id and stores it on
  // the \journal| parameter.
  Status CreateJournal(bool implicit,
                       const CommitId& base,
                       std::unique_ptr<Journal>* journal);
  // Creates a new |Journal| for a merge commit with |base| and |other as
  // parents. The result is stored on the \journal| parameter.
  Status CreateMergeJournal(const CommitId& base,
                            const CommitId& other,
                            std::unique_ptr<Journal>* journal);
  // Finds all implicit journal ids and replaces the contents of |journal_ids|
  // with their ids.
  Status GetImplicitJournalIds(std::vector<JournalId>* journal_ids);
  // Stores the implicit journal with the given |journal_id| in the |journal|
  // parameter.
  Status GetImplicitJournal(const JournalId& journal_id,
                            std::unique_ptr<Journal>* journal);
  // Removes all information on explicit journals from the database.
  Status RemoveExplicitJournals();
  // Removes all information on the journal with the given |journal_id| from the
  // database.
  Status RemoveJournal(const JournalId& journal_id);
  // Adds a new |key|-|value| pair with the given |priority| to the journal with
  // the given |journal_id|.
  Status AddJournalEntry(const JournalId& journal_id,
                         ftl::StringView key,
                         ftl::StringView value,
                         KeyPriority priority);
  // Removes the given key from the journal with the given |journal_id|.
  Status RemoveJournalEntry(const JournalId& journal_id,
                            const std::string& key);
  // Finds all the entries of the journal with the given |journal_id| and stores
  // an interator over the results on |entires|.
  Status GetJournalEntries(
      const JournalId& journal_id,
      std::unique_ptr<Iterator<const EntryChange>>* entries);

  // Finds the set of unsynced commits and replaces the contents of |commit_ids|
  // with their ids.
  Status GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids);
  // Marks the given |commit_id| as synced.
  Status MarkCommitIdSynced(const CommitId& commit_id);
  // Marks the given |commit_id| as unsynced.
  Status MarkCommitIdUnsynced(const CommitId& commit_id);
  // Checks if the commit with the given |commit_id| is synced.
  Status IsCommitSynced(const CommitId& commit_id, bool* is_synced);

  // Finds the set of unsynced objects and replaces the contentst of
  // |object_ids| with their ids.
  Status GetUnsyncedObjectIds(std::vector<ObjectId>* object_ids);
  // Marks the given |object_id| as synced.
  Status MarkObjectIdSynced(ObjectIdView object_id);
  // Marks the given |object_id| as unsynced.
  Status MarkObjectIdUnsynced(ObjectIdView object_id);
  // Checks if the object with the given |object_id| is synced.
  Status IsObjectSynced(ObjectIdView object_id, bool* is_synced);

 private:
  Status GetByPrefix(const leveldb::Slice& prefix,
                     std::vector<std::string>* keySuffixes);
  Status DeleteByPrefix(const leveldb::Slice& prefix);
  Status Get(const std::string& key, std::string* value);
  Status Put(const std::string& key, ftl::StringView value);
  Status Delete(const std::string& key);

  const std::string db_path_;
  std::unique_ptr<leveldb::DB> db_;

  const leveldb::WriteOptions write_options_;
  const leveldb::ReadOptions read_options_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DB);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_DB_H_
