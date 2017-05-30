// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_

#include "apps/ledger/src/storage/public/page_storage.h"

#include <queue>
#include <set>

#include "apps/ledger/src/callback/pending_operation.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/coroutine/coroutine.h"
#include "apps/ledger/src/storage/impl/db_impl.h"
#include "apps/ledger/src/storage/public/page_sync_delegate.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/ftl/tasks/task_runner.h"

namespace storage {

class PageStorageImpl : public PageStorage {
 public:
  PageStorageImpl(ftl::RefPtr<ftl::TaskRunner> main_runner,
                  ftl::RefPtr<ftl::TaskRunner> io_runner,
                  coroutine::CoroutineService* coroutine_service,
                  std::string page_dir,
                  PageId page_id);
  ~PageStorageImpl() override;

  // Initializes this PageStorageImpl. This includes initializing the underlying
  // database, adding the default page head if the page is empty, removing
  // uncommitted explicit and committing implicit journals.
  void Init(std::function<void(Status)> callback);

  // Adds the given locally created |commit| in this |PageStorage|.
  void AddCommitFromLocal(std::unique_ptr<const Commit> commit,
                          std::function<void(Status)> callback);

  // Returns true if the given |object_id| is untracked, i.e. has been  created
  // using |AddObjectFromLocal()|, but is not yet part of any commit. Untracked
  // objects are invalid after the PageStorageImpl object is destroyed.
  bool ObjectIsUntracked(ObjectIdView object_id);

  // Marks the given object as tracked.
  void MarkObjectTracked(ObjectIdView object_id);

  // PageStorage:
  PageId GetId() override;
  void SetSyncDelegate(PageSyncDelegate* page_sync) override;
  Status GetHeadCommitIds(std::vector<CommitId>* commit_ids) override;
  void GetCommit(CommitIdView commit_id,
                 std::function<void(Status, std::unique_ptr<const Commit>)>
                     callback) override;
  void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes,
                          std::function<void(Status)>) override;
  Status StartCommit(const CommitId& commit_id,
                     JournalType journal_type,
                     std::unique_ptr<Journal>* journal) override;
  Status StartMergeCommit(const CommitId& left,
                          const CommitId& right,
                          std::unique_ptr<Journal>* journal) override;
  Status AddCommitWatcher(CommitWatcher* watcher) override;
  Status RemoveCommitWatcher(CommitWatcher* watcher) override;
  void GetUnsyncedCommits(
      std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
          callback) override;
  Status MarkCommitSynced(const CommitId& commit_id) override;
  Status GetDeltaObjects(const CommitId& commit_id,
                         std::vector<ObjectId>* objects) override;
  void GetAllUnsyncedObjectIds(
      std::function<void(Status, std::vector<ObjectId>)> callback) override;
  void GetUnsyncedObjectIds(
      const CommitId& commit_id,
      std::function<void(Status, std::vector<ObjectId>)> callback) override;
  Status MarkObjectSynced(ObjectIdView object_id) override;
  void AddObjectFromLocal(
      std::unique_ptr<DataSource> data_source,
      const std::function<void(Status, ObjectId)>& callback) override;
  void GetObject(
      ObjectIdView object_id,
      Location location,
      const std::function<void(Status, std::unique_ptr<const Object>)>&
          callback) override;
  Status SetSyncMetadata(ftl::StringView key, ftl::StringView value) override;
  Status GetSyncMetadata(ftl::StringView key, std::string* value) override;

  // Commit contents.
  void GetCommitContents(const Commit& commit,
                         std::string min_key,
                         std::function<bool(Entry)> on_next,
                         std::function<void(Status)> on_done) override;
  void GetEntryFromCommit(const Commit& commit,
                          std::string key,
                          std::function<void(Status, Entry)> callback) override;
  void GetCommitContentsDiff(const Commit& base_commit,
                             const Commit& other_commit,
                             std::string min_key,
                             std::function<bool(EntryChange)> on_next_diff,
                             std::function<void(Status)> on_done) override;

 private:
  friend class PageStorageImplAccessorForTest;

  void AddCommits(std::vector<std::unique_ptr<const Commit>> commits,
                  ChangeSource source,
                  std::function<void(Status)> callback);
  Status ContainsCommit(CommitIdView id);
  bool IsFirstCommit(CommitIdView id);
  // Adds the given synced object. |object_id| will be validated against the
  // expected one based on the |data| and an |OBJECT_ID_MISSMATCH| error will be
  // returned in case of missmatch.
  void AddObjectFromSync(ObjectIdView object_id,
                         std::unique_ptr<DataSource> data_source,
                         std::function<void(Status)> callback);
  void AddObject(std::unique_ptr<DataSource> data_source,
                 const std::function<void(Status, ObjectId)>& callback);
  void GetObjectFromSync(
      ObjectIdView object_id,
      const std::function<void(Status, std::unique_ptr<const Object>)>&
          callback);
  std::string GetFilePath(ObjectIdView object_id) const;

  // Notifies the registered watchers with the |commits| in commit_to_send_.
  void NotifyWatchers();

  const ftl::RefPtr<ftl::TaskRunner> main_runner_;
  const ftl::RefPtr<ftl::TaskRunner> io_runner_;
  coroutine::CoroutineService* const coroutine_service_;
  const std::string page_dir_;
  const PageId page_id_;
  DbImpl db_;
  std::vector<CommitWatcher*> watchers_;
  std::set<ObjectId, convert::StringViewComparator> untracked_objects_;
  std::string objects_dir_;
  std::string staging_dir_;
  callback::PendingOperationManager pending_operation_manager_;
  PageSyncDelegate* page_sync_;
  std::queue<
      std::pair<ChangeSource, std::vector<std::unique_ptr<const Commit>>>>
      commits_to_send_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
