// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_DB_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_DB_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/storage/public/iterator.h"
#include "apps/ledger/src/storage/public/journal.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {

class PageStorageImpl;

// |DB| manages all Ledger related data that are stored in LevelDB. This
// includes commit objects, information on head commits, as well as metadata on
// on which objects and commits are not yet synchronized to the cloud.
class DB {
 public:
  class Batch {
   public:
    Batch() {}
    virtual ~Batch() {}

    virtual Status Execute() = 0;

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(Batch);
  };

  DB() {}
  virtual ~DB() {}

  // Initializes LevelDB or returns an |IO_ERROR| on failure.
  virtual Status Init() = 0;

  // Starts a LevelDB batch. Only one batch can be active at a time. The batch
  // will be written when the Execute is called on the returned object. The DB
  // object must outlive the batch object.
  virtual std::unique_ptr<Batch> StartBatch() = 0;

  // Heads.
  // Finds all head commits and replaces the contents of |heads| with their ids.
  // Returns |OK| on success or |IO_ERROR| in case of an error reading the
  // values. It is not an error if no heads are found.
  virtual Status GetHeads(std::vector<CommitId>* heads) = 0;

  // Adds the given |head| in the set of commit heads.
  virtual Status AddHead(const CommitId& head) = 0;

  // Removes the given |head| from the head commits.
  virtual Status RemoveHead(const CommitId& head) = 0;

  // Returns |OK| if the commit with the given |commit_id| is head commits or
  // |NOT_FOUND| if not.
  virtual Status ContainsHead(const CommitId& commit_id) = 0;

  // Commits.
  // Finds the commit with the given |commit_id| and stores its represenation in
  // storage bytes in the |storage_bytes| string.
  virtual Status GetCommitStorageBytes(const CommitId& commit_id,
                                       std::string* storage_bytes) = 0;

  // Adds the given |commit| in the database.
  virtual Status AddCommitStorageBytes(const CommitId& commit_id,
                                       const std::string& storage_bytes) = 0;

  // Removes the commit with the given |commit_id| from the commits.
  virtual Status RemoveCommit(const CommitId& commit_id) = 0;

  // Journals.
  // Creates a new |Journal| with the given |base| commit id and stores it on
  // the |journal| parameter.
  virtual Status CreateJournal(JournalType journal_type,
                               const CommitId& base,
                               std::unique_ptr<Journal>* journal) = 0;

  // Creates a new |Journal| for a merge commit with |base| and |other as
  // parents. The result is stored on the |journal| parameter.
  virtual Status CreateMergeJournal(const CommitId& base,
                                    const CommitId& other,
                                    std::unique_ptr<Journal>* journal) = 0;

  // Finds all implicit journal ids and replaces the contents of |journal_ids|
  // with their ids.
  virtual Status GetImplicitJournalIds(std::vector<JournalId>* journal_ids) = 0;

  // Stores the implicit journal with the given |journal_id| in the |journal|
  // parameter.
  virtual Status GetImplicitJournal(const JournalId& journal_id,
                                    std::unique_ptr<Journal>* journal) = 0;

  // Removes all information on explicit journals from the database.
  virtual Status RemoveExplicitJournals() = 0;

  // Removes all information on the journal with the given |journal_id| from the
  // database.
  virtual Status RemoveJournal(const JournalId& journal_id) = 0;

  // Adds a new |key|-|value| pair with the given |priority| to the journal with
  // the given |journal_id|.
  virtual Status AddJournalEntry(const JournalId& journal_id,
                                 ftl::StringView key,
                                 ftl::StringView value,
                                 KeyPriority priority) = 0;

  // Finds the value for the given |key| in the journal with the given id.
  virtual Status GetJournalValue(const JournalId& journal_id,
                                 ftl::StringView key,
                                 std::string* value) = 0;

  // Removes the given key from the journal with the given |journal_id|.
  virtual Status RemoveJournalEntry(const JournalId& journal_id,
                                    convert::ExtendedStringView key) = 0;

  // Journal value counters can be used to keep track of how many times a given
  // value is referenced in a journal.
  // Returns the number of times the given value is refererenced.
  virtual Status GetJournalValueCounter(const JournalId& journal_id,
                                        ftl::StringView value,
                                        int* counter) = 0;

  // Sets the number of times the given value is refererenced.
  virtual Status SetJournalValueCounter(const JournalId& journal_id,
                                        ftl::StringView value,
                                        int counter) = 0;

  // Returns the set of values that are refererenced in the given journal, i.e.
  // all values for which the journal value counter is a positive number.
  virtual Status GetJournalValues(const JournalId& journal_id,
                                  std::vector<std::string>* values) = 0;

  // Finds all the entries of the journal with the given |journal_id| and stores
  // an interator over the results on |entires|.
  virtual Status GetJournalEntries(
      const JournalId& journal_id,
      std::unique_ptr<Iterator<const EntryChange>>* entries) = 0;

  // Commit sync metadata.
  // Finds the set of unsynced commits and replaces the contents of |commit_ids|
  // with their ids. The result is ordered by the timestamps given when calling
  // |MarkCommitIdUnsynced|.
  virtual Status GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) = 0;

  // Marks the given |commit_id| as synced.
  virtual Status MarkCommitIdSynced(const CommitId& commit_id) = 0;

  // Marks the given |commit_id| as unsynced.
  virtual Status MarkCommitIdUnsynced(const CommitId& commit_id,
                                      int64_t timestamp) = 0;

  // Checks if the commit with the given |commit_id| is synced.
  virtual Status IsCommitSynced(const CommitId& commit_id, bool* is_synced) = 0;

  // Object sync metadata.
  // Finds the set of unsynced objects and replaces the contents of |object_ids|
  // with their ids. |object_ids| will be lexicographically sorted.
  virtual Status GetUnsyncedObjectIds(std::vector<ObjectId>* object_ids) = 0;

  // Marks the given |object_id| as synced.
  virtual Status MarkObjectIdSynced(ObjectIdView object_id) = 0;

  // Marks the given |object_id| as unsynced.
  virtual Status MarkObjectIdUnsynced(ObjectIdView object_id) = 0;

  // Checks if the object with the given |object_id| is synced.
  virtual Status IsObjectSynced(ObjectIdView object_id, bool* is_synced) = 0;

  // Tree node size.
  // Sets the node size of this page.
  virtual Status SetNodeSize(size_t node_size) = 0;

  // Finds the defined node size for this page and returns |OK| on success or
  // |NOT_FOUND| if the node_size is not defined, yet.
  virtual Status GetNodeSize(size_t* node_size) = 0;

  // Sets the opaque sync metadata associated with this page.
  virtual Status SetSyncMetadata(ftl::StringView sync_state) = 0;

  // Retrieves the opaque sync metadata associated with this page.
  virtual Status GetSyncMetadata(std::string* sync_state) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(DB);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_DB_H_
