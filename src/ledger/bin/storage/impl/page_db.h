// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_view.h"

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
  FXL_WARN_UNUSED_RESULT virtual Status AddHead(
      coroutine::CoroutineHandler* handler, CommitIdView head,
      zx::time_utc timestamp) = 0;

  // Removes the given |head| from the head commits.
  FXL_WARN_UNUSED_RESULT virtual Status RemoveHead(
      coroutine::CoroutineHandler* handler, CommitIdView head) = 0;

  // Merges.
  // Adds the commit with id |merge_commit_id| in the set of merges of commits
  // with ids |parent1_id| and |parent2_id|.
  FXL_WARN_UNUSED_RESULT virtual Status AddMerge(
      coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
      CommitIdView parent2_id, CommitIdView merge_commit_id) = 0;

  // Commits.
  // Adds the given |commit|, referencing |root_node|, in the database.
  FXL_WARN_UNUSED_RESULT virtual Status AddCommitStorageBytes(
      coroutine::CoroutineHandler* handler, const CommitId& commit_id,
      const ObjectIdentifier& root_node, fxl::StringView storage_bytes) = 0;

  // Object data.
  // Writes the content of the given object, and reference information from this
  // object to its |children|.
  FXL_WARN_UNUSED_RESULT virtual Status WriteObject(
      coroutine::CoroutineHandler* handler, const Piece& piece,
      PageDbObjectStatus object_status,
      const ObjectReferencesAndPriority& references) = 0;

  // Object sync metadata.
  // Sets the status of the object with the given id.
  FXL_WARN_UNUSED_RESULT virtual Status SetObjectStatus(
      coroutine::CoroutineHandler* handler,
      const ObjectIdentifier& object_identifier,
      PageDbObjectStatus object_status) = 0;

  // Commit sync metadata.
  // Marks the given |commit_id| as synced.
  FXL_WARN_UNUSED_RESULT virtual Status MarkCommitIdSynced(
      coroutine::CoroutineHandler* handler, const CommitId& commit_id) = 0;

  // Marks the given |commit_id| as unsynced.
  FXL_WARN_UNUSED_RESULT virtual Status MarkCommitIdUnsynced(
      coroutine::CoroutineHandler* handler, const CommitId& commit_id,
      uint64_t generation) = 0;

  // Sets the opaque sync metadata associated with this page for the given key.
  FXL_WARN_UNUSED_RESULT virtual Status SetSyncMetadata(
      coroutine::CoroutineHandler* handler, fxl::StringView key,
      fxl::StringView value) = 0;

  // Updates the online state of the page.
  FXL_WARN_UNUSED_RESULT virtual Status MarkPageOnline(
      coroutine::CoroutineHandler* handler) = 0;

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
    FXL_WARN_UNUSED_RESULT virtual Status Execute(
        coroutine::CoroutineHandler* handler) = 0;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Batch);
  };

  PageDb() {}
  ~PageDb() override {}

  // Starts a new batch. The batch will be written when Execute is called on the
  // returned object. The PageDb object must outlive the batch object. If the
  // coroutine is interrupted, |INTERRUPTED| status is returned.
  FXL_WARN_UNUSED_RESULT virtual Status StartBatch(
      coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) = 0;

  // Heads.
  // Finds all head commits and replaces the contents of |heads| with their ids.
  // Returns |OK| on success or |IO_ERROR| in case of an error reading the
  // values. It is not an error if no heads are found. The resulting |heads| are
  // ordered by the timestamp given at their insertion and if identical, by
  // their id.
  FXL_WARN_UNUSED_RESULT virtual Status GetHeads(
      coroutine::CoroutineHandler* handler,
      std::vector<std::pair<zx::time_utc, CommitId>>* heads) = 0;

  // Merges.
  // Finds all merges of the commits with ids |parent1_id| and |parent2_id|, and
  // returns their ids.
  FXL_WARN_UNUSED_RESULT virtual Status GetMerges(
      coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
      CommitIdView parent2_id, std::vector<CommitId>* heads) = 0;

  // Commits.
  // Finds the commit with the given |commit_id| and stores its represenation in
  // storage bytes in the |storage_bytes| string.
  FXL_WARN_UNUSED_RESULT virtual Status GetCommitStorageBytes(
      coroutine::CoroutineHandler* handler, CommitIdView commit_id,
      std::string* storage_bytes) = 0;

  // Piece data.
  // Reads the content of the given piece.
  FXL_WARN_UNUSED_RESULT virtual Status ReadObject(
      coroutine::CoroutineHandler* handler,
      const ObjectIdentifier& object_identifier,
      std::unique_ptr<const Piece>* piece) = 0;

  // Checks whether the object with the given |object_digest| is stored in the
  // database. Returns |OK| if the objet was found, or |INTERNAL_NOT_FOUND| if
  // not.
  FXL_WARN_UNUSED_RESULT virtual Status HasObject(
      coroutine::CoroutineHandler* handler,
      const ObjectIdentifier& object_identifier) = 0;

  // Returns the status of the object with the given id.
  FXL_WARN_UNUSED_RESULT virtual Status GetObjectStatus(
      coroutine::CoroutineHandler* handler,
      const ObjectIdentifier& object_identifier,
      PageDbObjectStatus* object_status) = 0;

  // Returns inbound object references towards the object with the given id.
  // WARNING: this function is reversing the usual semantics of
  // |ObjectReferencesAndPriority|. |references| contains |source| identifiers
  // such that there are references from |source| to |object_identifier|.
  FXL_WARN_UNUSED_RESULT virtual Status GetInboundObjectReferences(
      coroutine::CoroutineHandler* handler,
      const ObjectIdentifier& object_identifier,
      ObjectReferencesAndPriority* references) = 0;

  // Returns inbound commit references towards the object with the given id.
  FXL_WARN_UNUSED_RESULT virtual Status GetInboundCommitReferences(
      coroutine::CoroutineHandler* handler,
      const ObjectIdentifier& object_identifier,
      std::vector<CommitId>* references) = 0;

  // Commit sync metadata.
  // Finds the set of unsynced commits and replaces the contents of |commit_ids|
  // with their ids. The result is ordered by the timestamps given when calling
  // |MarkCommitIdUnsynced|.
  FXL_WARN_UNUSED_RESULT virtual Status GetUnsyncedCommitIds(
      coroutine::CoroutineHandler* handler,
      std::vector<CommitId>* commit_ids) = 0;

  // Checks if the commit with the given |commit_id| is synced.
  FXL_WARN_UNUSED_RESULT virtual Status IsCommitSynced(
      coroutine::CoroutineHandler* handler, const CommitId& commit_id,
      bool* is_synced) = 0;

  // Object sync metadata.
  // Finds the set of unsynced pieces and replaces the contents of
  // |object_identifiers| with their identifiers.
  FXL_WARN_UNUSED_RESULT virtual Status GetUnsyncedPieces(
      coroutine::CoroutineHandler* handler,
      std::vector<ObjectIdentifier>* object_identifiers) = 0;

  // Sync metadata.
  // Retrieves the opaque sync metadata associated with this page for the given
  // key.
  FXL_WARN_UNUSED_RESULT virtual Status GetSyncMetadata(
      coroutine::CoroutineHandler* handler, fxl::StringView key,
      std::string* value) = 0;

  // Returns whether the page is online, i.e. has been synced to the cloud or a
  // peer at least once from this device. By default, the state of a page is
  // offline. Once the state is set to online, it cannot be unset.
  FXL_WARN_UNUSED_RESULT virtual Status IsPageOnline(
      coroutine::CoroutineHandler* handler, bool* page_is_online) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageDb);
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_H_
