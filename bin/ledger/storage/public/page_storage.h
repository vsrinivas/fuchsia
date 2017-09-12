// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_PAGE_STORAGE_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_PAGE_STORAGE_H_

#include <functional>
#include <memory>
#include <utility>

#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/data_source.h"
#include "apps/ledger/src/storage/public/journal.h"
#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/page_sync_delegate.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace storage {

// |PageStorage| manages the local storage of a single page.
class PageStorage {
 public:
  struct CommitIdAndBytes {
    CommitIdAndBytes(CommitId id, std::string bytes);
    CommitIdAndBytes(CommitIdAndBytes&& other);

    CommitIdAndBytes& operator=(CommitIdAndBytes&& other);

    CommitId id;
    std::string bytes;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(CommitIdAndBytes);
  };

  // Location where to search an object. See |GetObject| call for usage.
  enum Location { LOCAL, NETWORK };

  PageStorage() {}
  virtual ~PageStorage() {}

  // Returns the id of this page.
  virtual PageId GetId() = 0;

  // Sets the PageSyncDelegate for this PageStorage. A nullptr can be passed to
  // unset a previously set value.
  virtual void SetSyncDelegate(PageSyncDelegate* page_sync) = 0;

  // Finds the ids of all head commits. It is guaranteed that valid pages have
  // at least one head commit, even if they are empty.
  virtual void GetHeadCommitIds(
      std::function<void(Status, std::vector<CommitId>)> callback) = 0;
  // Finds the commit with the given |commit_id| and calls the given |callback|
  // with the result.
  virtual void GetCommit(
      CommitIdView commit_id,
      std::function<void(Status, std::unique_ptr<const Commit>)> callback) = 0;

  // Adds a list of commits with the given ids and bytes to storage. The
  // callback is called when the storage has finished processing the commits. If
  // the status passed to the callback is OK, this indicates that storage
  // fetched all referenced objects and is ready to accept subsequent commits.
  virtual void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes,
                                  std::function<void(Status)> callback) = 0;
  // Starts a new journal based on the commit with the given |commit_id|. The
  // base commit must be one of the head commits. If |journal_type| is
  // |EXPLICIT|, all changes will be lost after a crash. Otherwise, changes to
  // implicit journals will be committed on system restart.
  virtual void StartCommit(
      const CommitId& commit_id,
      JournalType journal_type,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) = 0;
  // Starts a new journal for a merge commit, based on the given commits.
  // |left| and |right| must both be in the set of head commits. All
  // modifications to the journal consider the |left| as the base of the new
  // commit. Merge commits are always explicit, that is in case of a crash all
  // changes to the journal will be lost.
  virtual void StartMergeCommit(
      const CommitId& left,
      const CommitId& right,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) = 0;

  // Commits the given |journal| and when finished, returns the success/failure
  // status and the created Commit object through the given |callback|.
  virtual void CommitJournal(
      std::unique_ptr<Journal> journal,
      std::function<void(Status, std::unique_ptr<const Commit>)> callback) = 0;
  // Rolls back all changes to the given |Journal|.
  virtual void RollbackJournal(std::unique_ptr<Journal> journal,
                               std::function<void(Status)> callback) = 0;
  // Registers the given |CommitWatcher| which will be notified on new commits.
  virtual Status AddCommitWatcher(CommitWatcher* watcher) = 0;
  // Unregisters the given CommitWatcher.
  virtual Status RemoveCommitWatcher(CommitWatcher* watcher) = 0;

  // Finds the commits that have not yet been synced.
  //
  // The commits passed in the callback are sorted in a non-decreasing order of
  // their generations.
  virtual void GetUnsyncedCommits(
      std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
          callback) = 0;

  // Marks the given commit as synced.
  virtual void MarkCommitSynced(const CommitId& commit_id,
                                std::function<void(Status)> callback) = 0;

  // Finds all objects in the storage that are not yet synced, and calls
  // |callback| with the operation status and the corresponding |ObjectId|s
  // vector.
  virtual void GetUnsyncedPieces(
      std::function<void(Status, std::vector<ObjectId>)> callback) = 0;
  // Marks the object with the given |object_id| as synced.
  virtual void MarkPieceSynced(ObjectIdView object_id,
                               std::function<void(Status)> callback) = 0;
  // Adds the given local object and passes the new object's id to the callback.
  // If |size| is not negative, the content size must be equal to |size|,
  // otherwise the call will fail and return |IO_ERROR| in the callback. If
  // |size| is negative, no validation is done.
  virtual void AddObjectFromLocal(
      std::unique_ptr<DataSource> data_source,
      std::function<void(Status, ObjectId)> callback) = 0;
  // Finds the Object associated with the given |object_id|. The result or an
  // an error will be returned through the given |callback|. If |location| is
  // LOCAL, only local storage will be checked. If |location| is NETWORK, then
  // a network request may be made if the requested object is not present
  // locally.
  virtual void GetObject(
      ObjectIdView object_id,
      Location location,
      std::function<void(Status, std::unique_ptr<const Object>)> callback) = 0;
  // Finds the piece associated with the given |object_id|. The result or an an
  // error will be returned through the given |callback|. Only local storage is
  // checked, and if the object is an index, is it returned as is, and not
  // expanded.
  virtual void GetPiece(
      ObjectIdView object_id,
      std::function<void(Status, std::unique_ptr<const Object>)> callback) = 0;

  // Sets the opaque sync metadata associated with this page associated with the
  // given |key|. This state is persisted through restarts and can be retrieved
  // using |GetSyncMetadata()|.
  virtual void SetSyncMetadata(fxl::StringView key,
                               fxl::StringView value,
                               std::function<void(Status)> callback) = 0;

  // Retrieves the opaque sync metadata associated with this page and the given
  // |key|.
  virtual Status GetSyncMetadata(fxl::StringView key, std::string* value) = 0;

  // Commit contents.

  // Iterates over the entries of the given |commit| and calls |on_next| on
  // found entries with a key equal to or greater than |min_key|. Returning
  // false from |on_next| will immediately stop the iteration. |on_done| is
  // called once, upon successfull completion, i.e. when there are no more
  // elements or iteration was interrupted, or if an error occurs.
  virtual void GetCommitContents(const Commit& commit,
                                 std::string min_key,
                                 std::function<bool(Entry)> on_next,
                                 std::function<void(Status)> on_done) = 0;

  // Retrieves the entry with the given |key| and calls |on_done| with the
  // result. The status of |on_done| will be |OK| on success, |NOT_FOUND| if
  // there is no such key in the given commit or an error status on failure.
  virtual void GetEntryFromCommit(
      const Commit& commit,
      std::string key,
      std::function<void(Status, Entry)> on_done) = 0;

  // Iterates over the difference between the contents of two commits and calls
  // |on_next_diff| on found changed entries. Returning false from
  // |on_next_diff| will immediately stop the iteration. |on_done| is called
  // once, upon successfull completion, i.e. when there are no more differences
  // or iteration was interrupted, or if an error occurs.
  virtual void GetCommitContentsDiff(
      const Commit& base_commit,
      const Commit& other_commit,
      std::string min_key,
      std::function<bool(EntryChange)> on_next_diff,
      std::function<void(Status)> on_done) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageStorage);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_PAGE_STORAGE_H_
