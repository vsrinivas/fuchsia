// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_

#include "apps/ledger/src/storage/public/page_storage.h"

#include "apps/ledger/src/storage/impl/db_impl.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace storage {

class PageStorageImpl : public PageStorage {
 public:
  PageStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                  std::string page_dir,
                  PageIdView page_id);
  ~PageStorageImpl() override;

  // Initializes this PageStorageImpl. This includes initializing the underlying
  // database, adding the default page head if the page is empty, removing
  // uncommitted explicit and committing implicit journals.
  Status Init();

  // Adds the given locally created |commit| in this |PageStorage|.
  Status AddCommitFromLocal(std::unique_ptr<Commit> commit);

  // PageStorage:
  PageId GetId() override;
  Status GetHeadCommitIds(std::vector<CommitId>* commit_ids) override;
  Status GetCommit(const CommitId& commit_id,
                   std::unique_ptr<const Commit>* commit) override;
  Status AddCommitFromSync(const CommitId& id,
                           std::string&& storage_bytes) override;
  Status StartCommit(const CommitId& commit_id,
                     JournalType journal_type,
                     std::unique_ptr<Journal>* journal) override;
  Status StartMergeCommit(const CommitId& left,
                          const CommitId& right,
                          std::unique_ptr<Journal>* journal) override;
  Status AddCommitWatcher(CommitWatcher* watcher) override;
  Status RemoveCommitWatcher(CommitWatcher* watcher) override;
  Status GetUnsyncedCommits(
      std::vector<std::unique_ptr<const Commit>>* commits) override;
  Status MarkCommitSynced(const CommitId& commit_id) override;
  Status GetDeltaObjects(
      const CommitId& commit_id,
      std::vector<std::unique_ptr<const Object>>* objects) override;
  Status GetUnsyncedObjects(
      const CommitId& commit_id,
      std::vector<std::unique_ptr<const Object>>* objects) override;
  Status MarkObjectSynced(ObjectIdView object_id) override;
  void AddObjectFromSync(ObjectIdView object_id,
                         mojo::ScopedDataPipeConsumerHandle data,
                         size_t size,
                         const std::function<void(Status)>& callback) override;
  void AddObjectFromLocal(
      mojo::ScopedDataPipeConsumerHandle data,
      int64_t size,
      const std::function<void(Status, ObjectId)>& callback) override;
  void GetObject(
      ObjectIdView object_id,
      const std::function<void(Status, std::unique_ptr<const Object>)>&
          callback) override;
  Status GetObjectSynchronous(ObjectIdView object_id,
                              std::unique_ptr<const Object>* object) override;
  Status AddObjectSynchronous(convert::ExtendedStringView data,
                              std::unique_ptr<const Object>* object) override;

 private:
  class FileWriter;

  Status AddCommit(std::unique_ptr<const Commit> commit, ChangeSource source);

  // Notifies the registered watchers with the given |commit|.
  void NotifyWatchers(const Commit& commit, ChangeSource source);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::string page_dir_;
  PageId page_id_;
  DbImpl db_;
  std::vector<CommitWatcher*> watchers_;
  std::string objects_dir_;
  std::string staging_dir_;
  std::vector<std::unique_ptr<FileWriter>> writers_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
