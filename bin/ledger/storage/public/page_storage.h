// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_STORAGE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_STORAGE_H_

#include <functional>
#include <memory>
#include <utility>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/commit_watcher.h"
#include "peridot/bin/ledger/storage/public/data_source.h"
#include "peridot/bin/ledger/storage/public/journal.h"
#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_sync_client.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

// |PageStorage| manages the local storage of a single page.
class PageStorage : public PageSyncClient {
 public:
  struct CommitIdAndBytes {
    CommitIdAndBytes();
    CommitIdAndBytes(CommitId id, std::string bytes);
    CommitIdAndBytes(CommitIdAndBytes&& other) noexcept;

    CommitIdAndBytes& operator=(CommitIdAndBytes&& other) noexcept;

    CommitId id;
    std::string bytes;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(CommitIdAndBytes);
  };

  // Location where to search an object. See |GetObject| call for usage.
  enum Location { LOCAL, NETWORK };

  PageStorage() {}
  ~PageStorage() override {}

  // Returns the id of this page.
  virtual PageId GetId() = 0;

  // Finds the ids of all head commits. It is guaranteed that valid pages have
  // at least one head commit, even if they are empty.
  virtual void GetHeadCommitIds(
      fit::function<void(Status, std::vector<CommitId>)> callback) = 0;
  // Finds the commit with the given |commit_id| and calls the given |callback|
  // with the result.
  virtual void GetCommit(
      CommitIdView commit_id,
      fit::function<void(Status, std::unique_ptr<const Commit>)> callback) = 0;

  // Adds a list of commits with the given ids and bytes to storage. The
  // callback is called when the storage has finished processing the commits. If
  // the status passed to the callback is OK, this indicates that storage
  // fetched all referenced objects and is ready to accept subsequent commits.
  virtual void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes,
                                  ChangeSource source,
                                  fit::function<void(Status)> callback) = 0;
  // Starts a new journal based on the commit with the given |commit_id|. The
  // base commit must be one of the head commits. If |journal_type| is
  // |EXPLICIT|, all changes will be lost after a crash. Otherwise, changes to
  // implicit journals will be committed on system restart.
  virtual void StartCommit(
      const CommitId& commit_id, JournalType journal_type,
      fit::function<void(Status, std::unique_ptr<Journal>)> callback) = 0;
  // Starts a new journal for a merge commit, based on the given commits.
  // |left| and |right| must both be in the set of head commits. All
  // modifications to the journal consider the |left| as the base of the new
  // commit. Merge commits are always explicit, that is in case of a crash all
  // changes to the journal will be lost.
  virtual void StartMergeCommit(
      const CommitId& left, const CommitId& right,
      fit::function<void(Status, std::unique_ptr<Journal>)> callback) = 0;

  // Commits the given |journal| and when finished, returns the success/failure
  // status and the created Commit object through the given |callback|.
  virtual void CommitJournal(
      std::unique_ptr<Journal> journal,
      fit::function<void(Status, std::unique_ptr<const Commit>)> callback) = 0;
  // Rolls back all changes to the given |Journal|.
  virtual void RollbackJournal(std::unique_ptr<Journal> journal,
                               fit::function<void(Status)> callback) = 0;
  // Registers the given |CommitWatcher| which will be notified on new commits.
  virtual Status AddCommitWatcher(CommitWatcher* watcher) = 0;
  // Unregisters the given CommitWatcher.
  virtual Status RemoveCommitWatcher(CommitWatcher* watcher) = 0;

  // Checks whether there are any unsynced commits or pieces in this page. Note
  // that since the result is computed asynchronously, the caller must have
  // exclusive access to the page to ensure a correct result.
  virtual void IsSynced(fit::function<void(Status, bool)> callback) = 0;

  // Checks whether this page storage is empty. A page is not empty if there is
  // more than one head commits. Note that since the result is computed
  // asynchronously, the caller must have exclusive access to the page to ensure
  // a correct result.
  virtual void IsEmpty(fit::function<void(Status, bool)> callback) = 0;

  // Checks whether this page is online, i.e. has been synced to the cloud or a
  // peer. The page is marked as online if any of these has occured: a local
  // commit has been synced to the cloud, commits from the cloud have been
  // downloaded, or the page has been synced to a peer. Note that the result of
  // this method might be incorrect if there are other asynchronous operations
  // in progress. To ensure a correct result, the caller must have exclusive
  // access to the page.
  virtual bool IsOnline() = 0;

  // Finds the commits that have not yet been synced.
  //
  // The commits passed in the callback are sorted in a non-decreasing order of
  // their generations.
  virtual void GetUnsyncedCommits(
      fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
          callback) = 0;

  // Marks the given commit as synced.
  virtual void MarkCommitSynced(const CommitId& commit_id,
                                fit::function<void(Status)> callback) = 0;

  // Finds all objects in the storage that are not yet synced, and calls
  // |callback| with the operation status and the corresponding
  // |ObjectIdentifier|s vector.
  virtual void GetUnsyncedPieces(
      fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) = 0;
  // Marks the object with the given |object_identifier| as synced.
  virtual void MarkPieceSynced(ObjectIdentifier object_identifier,
                               fit::function<void(Status)> callback) = 0;
  // Returns true if the object is known to be synced to the cloud, false
  // otherwise.
  virtual void IsPieceSynced(ObjectIdentifier object_identifier,
                             fit::function<void(Status, bool)> callback) = 0;

  // Marks this page as synced to a peer.
  virtual void MarkSyncedToPeer(fit::function<void(Status)> callback) = 0;

  // Adds the given local object and passes the new object's id to the callback.
  virtual void AddObjectFromLocal(
      std::unique_ptr<DataSource> data_source,
      fit::function<void(Status, ObjectIdentifier)> callback) = 0;
  // Finds the Object associated with the given |object_identifier|. The result
  // or an an error will be returned through the given |callback|. If |location|
  // is LOCAL, only local storage will be checked. If |location| is NETWORK,
  // then a network request may be made if the requested object is not present
  // locally.
  virtual void GetObject(
      ObjectIdentifier object_identifier, Location location,
      fit::function<void(Status, std::unique_ptr<const Object>)> callback) = 0;
  // Finds the piece associated with the given |object_identifier|. The result
  // or an error will be returned through the given |callback|. Only local
  // storage is checked, and if the object is an index, is it returned as is,
  // and not expanded.
  virtual void GetPiece(
      ObjectIdentifier object_identifier,
      fit::function<void(Status, std::unique_ptr<const Object>)> callback) = 0;

  // Sets the opaque sync metadata associated with this page associated with the
  // given |key|. This state is persisted through restarts and can be retrieved
  // using |GetSyncMetadata()|.
  virtual void SetSyncMetadata(fxl::StringView key, fxl::StringView value,
                               fit::function<void(Status)> callback) = 0;

  // Retrieves the opaque sync metadata associated with this page and the given
  // |key|.
  virtual void GetSyncMetadata(
      fxl::StringView key,
      fit::function<void(Status, std::string)> callback) = 0;

  // Commit contents.

  // Iterates over the entries of the given |commit| and calls |on_next| on
  // found entries with a key equal to or greater than |min_key|. Returning
  // false from |on_next| will immediately stop the iteration. |on_done| is
  // called once, upon successfull completion, i.e. when there are no more
  // elements or iteration was interrupted, or if an error occurs.
  virtual void GetCommitContents(const Commit& commit, std::string min_key,
                                 fit::function<bool(Entry)> on_next,
                                 fit::function<void(Status)> on_done) = 0;

  // Retrieves the entry with the given |key| and calls |on_done| with the
  // result. The status of |on_done| will be |OK| on success, |NOT_FOUND| if
  // there is no such key in the given commit or an error status on failure.
  virtual void GetEntryFromCommit(
      const Commit& commit, std::string key,
      fit::function<void(Status, Entry)> on_done) = 0;

  // Iterates over the difference between the contents of two commits and calls
  // |on_next_diff| on found changed entries. Returning false from
  // |on_next_diff| will immediately stop the iteration. |on_done| is called
  // once, upon successfull completion, i.e. when there are no more differences
  // or iteration was interrupted, or if an error occurs.
  virtual void GetCommitContentsDiff(
      const Commit& base_commit, const Commit& other_commit,
      std::string min_key, fit::function<bool(EntryChange)> on_next_diff,
      fit::function<void(Status)> on_done) = 0;

  // Computes the 3-way diff between a base commit and two other commits. Calls
  // |on_next_diff| on found changed entries. Returning false from
  // |on_next_diff| will immediately stop the iteration. |on_done| is called
  // once, upon successfull completion, i.e. when there are no more differences
  // or iteration was interrupted, or if an error occurs.
  virtual void GetThreeWayContentsDiff(
      const Commit& base_commit, const Commit& left_commit,
      const Commit& right_commit, std::string min_key,
      fit::function<bool(ThreeWayChange)> on_next_diff,
      fit::function<void(Status)> on_done) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageStorage);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_PAGE_STORAGE_H_
