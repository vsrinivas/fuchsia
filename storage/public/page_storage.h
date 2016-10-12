// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_PAGE_STORAGE_H_
#define APPS_LEDGER_STORAGE_PUBLIC_PAGE_STORAGE_H_

#include <functional>
#include <memory>

#include "apps/ledger/storage/public/blob.h"
#include "apps/ledger/storage/public/commit.h"
#include "apps/ledger/storage/public/commit_watcher.h"
#include "apps/ledger/storage/public/journal.h"
#include "apps/ledger/storage/public/object.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace storage {

// |PageStorage| manages the local storage of a single page.
class PageStorage {
 public:
  PageStorage() {}
  virtual ~PageStorage() {}

  // Returns the id of this page.
  virtual PageId GetId() = 0;

  // Sets a handler for the case of page deletion. After a page has been deleted
  // all corresponding commits, objects and journals are no longer valid, and
  // any call to any of this |PageStorage|'s methods will fail with a
  // |PAGE_DELETED| error.
  virtual void SetPageDeletionHandler(
      const std::function<void()>& on_page_deletion) = 0;

  // Finds the ids of all head commits and adds them in the |commit_ids| vector.
  // It is guaranteed that valid pages have at least one head commit, even if
  // they are empty.
  virtual Status GetHeadCommitIds(std::vector<CommitId>* commit_ids) = 0;
  // Finds the commit with the given |commit_id| and stores the value in
  // |commit|.
  virtual Status GetCommit(const CommitId& commit_id,
                           std::unique_ptr<Commit>* commit) = 0;

  // Adds the given synced |commit| in this |PageStorage|.
  virtual Status AddCommitFromSync(const CommitId& id,
                                   const std::string& storage_bytes) = 0;
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
      std::vector<std::unique_ptr<Commit>>* commits) = 0;
  // Marks the given commit as synced.
  virtual Status MarkCommitSynced(const CommitId& commit_id) = 0;

  // Finds all objects introduced by the commit with the given |commit_id| and
  // adds them in the given |objects| vector. This includes all objects present
  // in the storage tree of the commit that were not in storage tree of its
  // parent(s).
  virtual Status GetDeltaObjects(const CommitId& commit_id,
                                 std::vector<Object>* objects) = 0;
  // Finds all objects in the storage tree of the commit with the given
  // |commit_id| that are not yet synced and adds them in the |objectus| vector.
  virtual Status GetUnsyncedObjects(const CommitId& commit_id,
                                    std::vector<Object>* objects) = 0;
  // Marks the object with the given |object_id| as synced.
  virtual Status MarkObjectSynced(ObjectIdView object_id) = 0;
  // Stores the given synced object. \object_id\ will be validated against the
  // expected one based on the \data\ and an |OBJECT_ID_MISSMATCH| error will be
  // returned in case of missmatch.
  virtual Status AddObjectFromSync(ObjectIdView object_id,
                                   mojo::DataPipeConsumerHandle data,
                                   size_t size) = 0;
  // Stores the given local object and stores the new object's id in the
  // |object_id| parameter.
  virtual Status AddObjectFromLocal(mojo::DataPipeConsumerHandle data,
                                    size_t size,
                                    ObjectId* object_id) = 0;

  // Finds the Blob associated with the given |blob_id|. The result or an
  // an error will be returned through the given |callback|.
  virtual void GetBlob(
      ObjectIdView blob_id,
      const std::function<void(Status status, std::unique_ptr<Blob> blob)>
          callback) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageStorage);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_PAGE_STORAGE_H_
