// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_PAGE_STORAGE_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_PAGE_STORAGE_H_

#include <functional>
#include <memory>
#include <utility>

#include <mx/datapipe.h>

#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/commit_watcher.h"
#include "apps/ledger/src/storage/public/journal.h"
#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/page_sync_delegate.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {

// |PageStorage| manages the local storage of a single page.
class PageStorage {
 public:
  struct CommitIdAndBytes {
    CommitIdAndBytes(CommitId id, std::string bytes);
    CommitIdAndBytes(CommitIdAndBytes&&);

    CommitIdAndBytes& operator=(CommitIdAndBytes&&);

    CommitId id;
    std::string bytes;

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(CommitIdAndBytes);
  };

  PageStorage() {}
  virtual ~PageStorage() {}

  // Returns the id of this page.
  virtual PageId GetId() = 0;

  // Sets the PageSyncDelegate for this PageStorage. A nullptr can be passed to
  // unset a previously set value.
  virtual void SetSyncDelegate(PageSyncDelegate* page_sync) = 0;

  // Finds the ids of all head commits and adds them in the |commit_ids| vector.
  // It is guaranteed that valid pages have at least one head commit, even if
  // they are empty.
  virtual Status GetHeadCommitIds(std::vector<CommitId>* commit_ids) = 0;
  // Finds the commit with the given |commit_id| and stores the value in
  // |commit|.
  virtual Status GetCommit(const CommitId& commit_id,
                           std::unique_ptr<const Commit>* commit) = 0;

  // Adds a list of commits with the given ids and bytes to storage. The
  // callback is called when the storage has finished processing the commits. If
  // the status passed to the callback is OK, this indicates that storage
  // fetched all referenced objects and is ready to accept subsequent commits.
  virtual void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes,
                                  std::function<void(Status)> callback) = 0;
  // Starts a new |journal| based on the commit with the given |commit_id|. The
  // base commit must be one of  the head commits. If |implicit| is false all
  // changes will be lost after a crash. Otherwise, changes to implicit
  // journals will be committed on system restart.
  virtual Status StartCommit(const CommitId& commit_id,
                             JournalType journal_type,
                             std::unique_ptr<Journal>* journal) = 0;
  // Starts a new |journal| for a merge commit, based on the given commits.
  // |left| and |right| must both be in the set of head commits. All
  // modifications to the journal consider the |left| as the base of the new
  // commit. Merge commits are always explicit, that is in case of a crash all
  // changes to the journal will be lost.
  virtual Status StartMergeCommit(const CommitId& left,
                                  const CommitId& right,
                                  std::unique_ptr<Journal>* journal) = 0;

  // Registers the given |CommitWatcher| which will be notified on new commits.
  virtual Status AddCommitWatcher(CommitWatcher* watcher) = 0;
  // Unregisters the given CommitWatcher.
  virtual Status RemoveCommitWatcher(CommitWatcher* watcher) = 0;

  // Finds the commits that have not yet been synced and adds them in the given
  // |commit| vector.
  virtual Status GetUnsyncedCommits(
      std::vector<std::unique_ptr<const Commit>>* commits) = 0;
  // Marks the given commit as synced.
  virtual Status MarkCommitSynced(const CommitId& commit_id) = 0;

  // Finds all objects introduced by the commit with the given |commit_id| and
  // adds them in the given |objects| vector. This includes all objects present
  // in the storage tree of the commit that were not in storage tree of its
  // parent(s).
  virtual Status GetDeltaObjects(const CommitId& commit_id,
                                 std::vector<ObjectId>* objects) = 0;
  // Finds all objects in the storage tree of the commit with the given
  // |commit_id| that are not yet synced and adds them in the |objectus| vector.
  virtual Status GetUnsyncedObjects(const CommitId& commit_id,
                                    std::vector<ObjectId>* objects) = 0;
  // Marks the object with the given |object_id| as synced.
  virtual Status MarkObjectSynced(ObjectIdView object_id) = 0;
  // Adds the given synced object. |object_id| will be validated against the
  // expected one based on the |data| and an |OBJECT_ID_MISSMATCH| error will be
  // returned in case of missmatch.
  virtual void AddObjectFromSync(
      ObjectIdView object_id,
      mx::datapipe_consumer data,
      size_t size,
      const std::function<void(Status)>& callback) = 0;
  // Adds the given local object and passes the new object's id to the callback.
  // If |size| is not negative, the content size must be equal to |size|,
  // otherwise the call will fail and return |IO_ERROR| in the callback. If
  // |size| is negative, no validation is done.
  virtual void AddObjectFromLocal(
      mx::datapipe_consumer data,
      int64_t size,
      const std::function<void(Status, ObjectId)>& callback) = 0;
  // Finds the Object associated with the given |object_id|. The result or an
  // an error will be returned through the given |callback|.
  virtual void GetObject(
      ObjectIdView object_id,
      const std::function<void(Status, std::unique_ptr<const Object>)>&
          callback) = 0;

  // Synchronous access to the store. These methods are a stop-gap to implement
  // the first version of the Ledger and should be removed. See: LE-31.
  virtual Status GetObjectSynchronous(
      ObjectIdView object_id,
      std::unique_ptr<const Object>* object) = 0;
  virtual Status AddObjectSynchronous(
      convert::ExtendedStringView data,
      std::unique_ptr<const Object>* object) = 0;

  // Sets the opaque sync metadata associated with this page. This state is
  // persisted through restarts and can be retrieved using |GetSyncMetadata()|.
  virtual Status SetSyncMetadata(ftl::StringView sync_state) = 0;

  // Retrieves the opaque sync metadata associated with this page.
  virtual Status GetSyncMetadata(std::string* sync_state) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageStorage);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_PAGE_STORAGE_H_
