// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_STORAGE_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_STORAGE_H_

#include <lib/fit/function.h>

#include <functional>
#include <memory>
#include <utility>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/commit_watcher.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/journal.h"
#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/page_sync_client.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

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
  class Location {
   public:
    Location();

    // Object should only be searched locally.
    static Location Local();
    // Object is from a value and should be searched both locally and from the network.
    //
    // Compatibility: during the transition to diffs, using |ValueFromNetwork| as a location for a
    // tree node is valid, and indicates that this object is expected to be present in the cloud and
    // should be fetched using |GetObject|.
    // TODO(LE-823): remove once transition to diffs is complete.
    static Location ValueFromNetwork();
    // Object is from a tree node and should be searched both locally and from the
    // network. |in_commit| is the identifier of a commit that has the object as part of a tree
    // node.
    static Location TreeNodeFromNetwork(CommitId in_commit);

    bool is_local() const;
    bool is_value_from_network() const;
    bool is_tree_node_from_network() const;
    bool is_network() const;
    const CommitId& in_commit() const;

   private:
    enum class Tag {
      LOCAL,
      NETWORK_VALUE,
      NETWORK_TREE_NODE,
    };

    Location(Tag flag, CommitId in_commit);
    friend bool operator==(const Location& lhs, const Location& rhs);
    friend bool operator<(const Location& lhs, const Location& rhs);

    Tag tag_;
    // Only valid if |tag_| is |NETWORK_TREE_NODE|.
    CommitId in_commit_;
  };

  PageStorage() = default;
  ~PageStorage() override = default;

  // Returns the id of this page.
  virtual PageId GetId() = 0;

  // Returns the ObjectIdentifierFactory associated with this page. |PageStorage| must outlive the
  // returned pointer.
  virtual ObjectIdentifierFactory* GetObjectIdentifierFactory() = 0;

  // Finds all head commits. It is guaranteed that valid pages have at least one
  // head commit, even if they are empty. The returned list is sorted according
  // to |Commit::TimestampOrdered|.
  virtual Status GetHeadCommits(std::vector<std::unique_ptr<const Commit>>* head_commits) = 0;
  // Finds the ids of all merge commits that have as parents the commits with
  // ids |parent1_id| and |parent2_id| and calls the given |callback| with the
  // result.
  virtual void GetMergeCommitIds(CommitIdView parent1_id, CommitIdView parent2_id,
                                 fit::function<void(Status, std::vector<CommitId>)> callback) = 0;
  // Finds the commit with the given |commit_id| and calls the given |callback|
  // with the result. |PageStorage| must outlive any |Commit| obtained through
  // it.
  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=35416): What status is passed to the
  // callback in the commit-was-garbage-collected circumstance?
  virtual void GetCommit(CommitIdView commit_id,
                         fit::function<void(Status, std::unique_ptr<const Commit>)> callback) = 0;

  // Returns the generation of the given commit, and the list of its parents that are not present in
  // local storage.
  virtual void GetGenerationAndMissingParents(
      const CommitIdAndBytes& id_and_bytes,
      fit::function<void(Status, uint64_t, std::vector<CommitId>)> callback) = 0;

  // Adds a list of commits with the given ids and bytes to storage. The
  // callback is called when the storage has finished processing the commits. If
  // the status passed to the callback is OK, this indicates that storage
  // fetched all referenced objects and is ready to accept subsequent commits.
  virtual void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source,
                                  fit::function<void(Status)> callback) = 0;
  // Starts a new journal based on the commit with the given |commit_id|. The
  // base commit must be one of the head commits. |PageStorage| must outlive any
  // |Journal| obtained through it.
  virtual std::unique_ptr<Journal> StartCommit(std::unique_ptr<const Commit> commit_id) = 0;
  // Starts a new journal for a merge commit, based on the given commits.
  // |left| and |right| must both be in the set of head commits. All
  // modifications to the journal consider the |left| as the base of the new
  // commit. Merge commits are always explicit, that is in case of a crash all
  // changes to the journal will be lost. |PageStorage| must outlive any
  // |Journal| obtained through it.
  virtual std::unique_ptr<Journal> StartMergeCommit(std::unique_ptr<const Commit> left,
                                                    std::unique_ptr<const Commit> right) = 0;

  // Commits the given |journal| and when finished, returns the success/failure
  // status and the created Commit object through the given |callback|. If the
  // operation is a no-op, the returned commit will be null. |PageStorage| must
  // outlive any |Commit| obtained through it.
  virtual void CommitJournal(
      std::unique_ptr<Journal> journal,
      fit::function<void(Status, std::unique_ptr<const Commit>)> callback) = 0;

  // Registers the given |CommitWatcher| which will be notified on new commits.
  // A given |CommitWatcher| must not be added more than once.
  virtual void AddCommitWatcher(CommitWatcher* watcher) = 0;
  // Unregisters the given |CommitWatcher|, if present.
  virtual void RemoveCommitWatcher(CommitWatcher* watcher) = 0;

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
  // their generations. |PageStorage| must outlive any |Commit| obtained through
  // it.
  virtual void GetUnsyncedCommits(
      fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)> callback) = 0;

  // Marks the given commit as synced.
  virtual void MarkCommitSynced(const CommitId& commit_id,
                                fit::function<void(Status)> callback) = 0;

  // Finds all objects in the storage that are not yet synced, and calls
  // |callback| with the operation status and the corresponding
  // |ObjectIdentifier|s vector.
  //
  // The objects are not guaranteed to still exist: they might have been just been garbage
  // collected.
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
  // |tree_references| are the BTree-level references (eg. references from the
  // node to its BTree children and values) if |object_type| is TREE_NODE, and
  // must be empty otherwise.
  virtual void AddObjectFromLocal(ObjectType object_type, std::unique_ptr<DataSource> data_source,
                                  ObjectReferencesAndPriority tree_references,
                                  fit::function<void(Status, ObjectIdentifier)> callback) = 0;

  // Finds the Object associated with the given |object_identifier|. The result
  // or an an error will be returned through the given |callback|. If |location|
  // is LOCAL, only local storage will be checked. If |location| is NETWORK,
  // then a network request may be made if the requested object is not present
  // locally.
  virtual void GetObject(ObjectIdentifier object_identifier, Location location,
                         fit::function<void(Status, std::unique_ptr<const Object>)> callback) = 0;

  // Retrieves a part of an object of type BLOB, starting at |offset| with a
  // maximum size of |max_size|, and maps it to a VMO.
  // If |offset| is less than 0, starts from |-offset| from the end of the
  // value. If |max_size| is less than 0, retrieves everything until the end of
  // an object.
  // This method must not be called on TREE_NODE objects.
  virtual void GetObjectPart(ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
                             Location location,
                             fit::function<void(Status, fsl::SizedVmo)> callback) = 0;

  // Finds the piece associated with the given |object_identifier|. The result
  // or an error will be returned through the given |callback|. Only local
  // storage is checked, and if the object is an index, is it returned as is,
  // and not expanded. The piece is guaranteed to remain available in storage as
  // long as the returned token is alive. The returned token must be nullptr if
  // and only if the returned piece is nullptr.
  virtual void GetPiece(ObjectIdentifier object_identifier,
                        fit::function<void(Status, std::unique_ptr<const Piece>)> callback) = 0;

  // Sets the opaque sync metadata associated with this page associated with the
  // given |key|. This state is persisted through restarts and can be retrieved
  // using |GetSyncMetadata()|.
  virtual void SetSyncMetadata(fxl::StringView key, fxl::StringView value,
                               fit::function<void(Status)> callback) = 0;

  // Retrieves the opaque sync metadata associated with this page and the given
  // |key|.
  virtual void GetSyncMetadata(fxl::StringView key,
                               fit::function<void(Status, std::string)> callback) = 0;

  // Commit contents.

  // Iterates over the entries of the given |commit| and calls |on_next| on
  // found entries with a key equal to or greater than |min_key|. Returning
  // false from |on_next| will immediately stop the iteration. |on_done| is
  // called once, upon successful completion, i.e. when there are no more
  // elements or iteration was interrupted, or if an error occurs.
  virtual void GetCommitContents(const Commit& commit, std::string min_key,
                                 fit::function<bool(Entry)> on_next,
                                 fit::function<void(Status)> on_done) = 0;

  // Retrieves the entry with the given |key| and calls |on_done| with the
  // result. The status of |on_done| will be |OK| on success, |KEY_NOT_FOUND| if
  // there is no such key in the given commit or an error status on failure.
  virtual void GetEntryFromCommit(const Commit& commit, std::string key,
                                  fit::function<void(Status, Entry)> on_done) = 0;

  // Diff used for the cloud provider.

  // Computes the diff between the |target_commit| and another commit, available locally. |callback|
  // is called with the status of the operation, the id of the commit used as the base of the diff
  // and the vector of all changes. Note that updating an entry will add two values in the vector of
  // changes: one deleting the previous entry and one adding the next one.
  virtual void GetDiffForCloud(
      const Commit& target_commit,
      fit::function<void(Status, CommitIdView, std::vector<EntryChange>)> callback) = 0;

  // Diffs for Merging and other client-facing usages.

  // Iterates over the difference between the contents of two commits and calls |on_next_diff| on
  // found changed entries. Returning false from |on_next_diff| will immediately stop the iteration.
  // |on_done| is called once, upon successful completion, i.e. when there are no more differences
  // or iteration was interrupted, or if an error occurs.
  virtual void GetCommitContentsDiff(const Commit& base_commit, const Commit& other_commit,
                                     std::string min_key,
                                     fit::function<bool(EntryChange)> on_next_diff,
                                     fit::function<void(Status)> on_done) = 0;

  // Computes the 3-way diff between a base commit and two other commits. Calls |on_next_diff| on
  // found changed entries. Returning false from |on_next_diff| will immediately stop the iteration.
  // |on_done| is called once, upon successful completion, i.e. when there are no more differences
  // or iteration was interrupted, or if an error occurs.
  virtual void GetThreeWayContentsDiff(const Commit& base_commit, const Commit& left_commit,
                                       const Commit& right_commit, std::string min_key,
                                       fit::function<bool(ThreeWayChange)> on_next_diff,
                                       fit::function<void(Status)> on_done) = 0;

  // Gets the current clock for this page.
  virtual void GetClock(fit::function<void(Status, Clock)> callback) = 0;

  // Finds the commit id of the commit with the given |remote_commit_id|.
  virtual void GetCommitIdFromRemoteId(fxl::StringView remote_commit_id,
                                       fit::function<void(Status, CommitId)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageStorage);
};

bool operator==(const PageStorage::CommitIdAndBytes& lhs, const PageStorage::CommitIdAndBytes& rhs);
bool operator==(const PageStorage::Location& lhs, const PageStorage::Location& rhs);
bool operator!=(const PageStorage::Location& lhs, const PageStorage::Location& rhs);
bool operator<(const PageStorage::Location& lhs, const PageStorage::Location& rhs);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_PAGE_STORAGE_H_
