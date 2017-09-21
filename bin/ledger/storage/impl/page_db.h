// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/coroutine/coroutine.h"
#include "apps/ledger/src/storage/impl/db.h"
#include "apps/ledger/src/storage/public/data_source.h"
#include "apps/ledger/src/storage/public/iterator.h"
#include "apps/ledger/src/storage/public/journal.h"
#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_view.h"

namespace storage {

class PageStorageImpl;

// Status of an object in the database.
enum class PageDbObjectStatus {
  // The object is not in the database.
  UNKNOWN,
  // The object is in the database, but not in any commit.
  TRANSIENT,
  // The object is associated to a commit, but not yet synced.
  LOCAL,
  // The object is synced.
  SYNCED,
};

// |PageDbMutator| provides all update (insertion and deletion) operations
// over |PageDb|.
class PageDbMutator {
 public:
  PageDbMutator() {}
  virtual ~PageDbMutator() {}

  // Heads.
  // Adds the given |head| in the set of commit heads.
  virtual Status AddHead(coroutine::CoroutineHandler* handler,
                         CommitIdView head,
                         int64_t timestamp) = 0;

  // Removes the given |head| from the head commits.
  virtual Status RemoveHead(coroutine::CoroutineHandler* handler,
                            CommitIdView head) = 0;

  // Commits.
  // Adds the given |commit| in the database.
  virtual Status AddCommitStorageBytes(coroutine::CoroutineHandler* handler,
                                       const CommitId& commit_id,
                                       fxl::StringView storage_bytes) = 0;

  // Removes the commit with the given |commit_id| from the commits.
  virtual Status RemoveCommit(coroutine::CoroutineHandler* handler,
                              const CommitId& commit_id) = 0;

  // Journals.
  // Creates a new id for a journal with the given type and base commit. In a
  // merge journal, the base commit is always the left one.
  virtual Status CreateJournalId(coroutine::CoroutineHandler* handler,
                                 JournalType journal_type,
                                 const CommitId& base,
                                 JournalId* journal_id) = 0;

  // Removes all information on explicit journals from the database.
  virtual Status RemoveExplicitJournals(
      coroutine::CoroutineHandler* handler) = 0;

  // Removes all information on the journal with the given |journal_id| from the
  // database.
  virtual Status RemoveJournal(coroutine::CoroutineHandler* handler,
                               const JournalId& journal_id) = 0;

  // Adds a new |key|-|value| pair with the given |priority| to the journal with
  // the given |journal_id|.
  virtual Status AddJournalEntry(coroutine::CoroutineHandler* handler,
                                 const JournalId& journal_id,
                                 fxl::StringView key,
                                 fxl::StringView value,
                                 KeyPriority priority) = 0;

  // Removes the given key from the journal with the given |journal_id|.
  virtual Status RemoveJournalEntry(coroutine::CoroutineHandler* handler,
                                    const JournalId& journal_id,
                                    convert::ExtendedStringView key) = 0;

  // Object data.
  // Writes the content of the given object.
  virtual Status WriteObject(coroutine::CoroutineHandler* handler,
                             ObjectIdView object_id,
                             std::unique_ptr<DataSource::DataChunk> content,
                             PageDbObjectStatus object_status) = 0;

  // Deletes the object with the given identifier.
  virtual Status DeleteObject(coroutine::CoroutineHandler* handler,
                              ObjectIdView object_id) = 0;

  // Object sync metadata.
  // Sets the status of the object with the given id.
  virtual Status SetObjectStatus(coroutine::CoroutineHandler* handler,
                                 ObjectIdView object_id,
                                 PageDbObjectStatus object_status) = 0;

  // Commit sync metadata.
  // Marks the given |commit_id| as synced.
  virtual Status MarkCommitIdSynced(coroutine::CoroutineHandler* handler,
                                    const CommitId& commit_id) = 0;

  // Marks the given |commit_id| as unsynced.
  virtual Status MarkCommitIdUnsynced(coroutine::CoroutineHandler* handler,
                                      const CommitId& commit_id,
                                      uint64_t generation) = 0;

  // Sets the opaque sync metadata associated with this page for the given key.
  virtual Status SetSyncMetadata(coroutine::CoroutineHandler* handler,
                                 fxl::StringView key,
                                 fxl::StringView value) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageDbMutator);
};

// |PageDb| manages all Ledger related data that are locally stored. This
// includes commit, value and tree node objects, information on head commits, as
// well as metadata on which objects and commits are not yet synchronized to the
// cloud.
class PageDb : public PageDbMutator {
 public:
  // A |Batch| can be used to execute a number of updates in |PageDb|
  // atomically.
  class Batch : public PageDbMutator {
   public:
    Batch() {}
    ~Batch() override {}

    // Executes this batch. No further operations in this batch are supported
    // after a successful execution.
    virtual Status Execute() = 0;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Batch);
  };

  PageDb() {}
  ~PageDb() override {}

  // Initializes PageDb or returns an |IO_ERROR| on failure.
  virtual Status Init() = 0;

  // Starts a new batch. The batch will be written when the Execute is called on
  // the returned object. The PageDb object must outlive the batch object.
  virtual std::unique_ptr<Batch> StartBatch() = 0;

  // Heads.
  // Finds all head commits and replaces the contents of |heads| with their ids.
  // Returns |OK| on success or |IO_ERROR| in case of an error reading the
  // values. It is not an error if no heads are found. The resulting |heads| are
  // ordered by the timestamp given at their insertion and if identical, by
  // their id.
  virtual Status GetHeads(coroutine::CoroutineHandler* handler,
                          std::vector<CommitId>* heads) = 0;

  // Commits.
  // Finds the commit with the given |commit_id| and stores its represenation in
  // storage bytes in the |storage_bytes| string.
  virtual Status GetCommitStorageBytes(coroutine::CoroutineHandler* handler,
                                       CommitIdView commit_id,
                                       std::string* storage_bytes) = 0;

  // Journals.
  // Finds all implicit journal ids and replaces the contents of |journal_ids|
  // with their ids.
  virtual Status GetImplicitJournalIds(coroutine::CoroutineHandler* handler,
                                       std::vector<JournalId>* journal_ids) = 0;

  // Stores the id of the base commit for the journal with the given |base|
  // parameter.
  virtual Status GetBaseCommitForJournal(coroutine::CoroutineHandler* handler,
                                         const JournalId& journal_id,
                                         CommitId* base) = 0;

  // Finds all the entries of the journal with the given |journal_id| and stores
  // an interator over the results on |entires|.
  virtual Status GetJournalEntries(
      coroutine::CoroutineHandler* handler,
      const JournalId& journal_id,
      std::unique_ptr<Iterator<const EntryChange>>* entries) = 0;

  // Object data.
  // Reads the content of the given object. To check whether an object is stored
  // in the PageDb without retrieving its value, |nullptr| can be given for the
  // |object| argument.
  virtual Status ReadObject(coroutine::CoroutineHandler* handler,
                            ObjectId object_id,
                            std::unique_ptr<const Object>* object) = 0;

  // Checks whether the object with the given |object_id| is stored in the
  // database.
  virtual Status HasObject(coroutine::CoroutineHandler* handler,
                           ObjectIdView object_id,
                           bool* has_object) = 0;

  // Returns the status of the object with the given id.
  virtual Status GetObjectStatus(coroutine::CoroutineHandler* handler,
                                 ObjectIdView object_id,
                                 PageDbObjectStatus* object_status) = 0;

  // Commit sync metadata.
  // Finds the set of unsynced commits and replaces the contents of |commit_ids|
  // with their ids. The result is ordered by the timestamps given when calling
  // |MarkCommitIdUnsynced|.
  virtual Status GetUnsyncedCommitIds(coroutine::CoroutineHandler* handler,
                                      std::vector<CommitId>* commit_ids) = 0;

  // Checks if the commit with the given |commit_id| is synced.
  virtual Status IsCommitSynced(coroutine::CoroutineHandler* handler,
                                const CommitId& commit_id,
                                bool* is_synced) = 0;

  // Object sync metadata.
  // Finds the set of unsynced pieces and replaces the contents of |object_ids|
  // with their ids. |object_ids| will be lexicographically sorted.
  virtual Status GetUnsyncedPieces(coroutine::CoroutineHandler* handler,
                                   std::vector<ObjectId>* object_ids) = 0;

  // Sync metadata.
  // Retrieves the opaque sync metadata associated with this page for the given
  // key.
  virtual Status GetSyncMetadata(coroutine::CoroutineHandler* handler,
                                 fxl::StringView key,
                                 std::string* value) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageDb);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_H_
