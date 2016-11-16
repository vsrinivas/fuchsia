// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_TEST_PAGE_STORAGE_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_TEST_PAGE_STORAGE_EMPTY_IMPL_H_

#include <functional>
#include <memory>
#include <vector>

#include "apps/ledger/src/storage/public/page_storage.h"

namespace storage {
namespace test {

// Empty implementaton of PageStorage. All methods do nothing and return dummy
// or empty responses.
class PageStorageEmptyImpl : public PageStorage {
 public:
  PageStorageEmptyImpl() = default;
  ~PageStorageEmptyImpl() override = default;

  // PageStorage:
  PageId GetId() override;

  void SetSyncDelegate(PageSyncDelegate* page_sync) override;

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

  Status GetDeltaObjects(const CommitId& commit_id,
                         std::vector<ObjectId>* objects) override;

  Status GetUnsyncedObjects(const CommitId& commit_id,
                            std::vector<ObjectId>* objects) override;

  Status MarkObjectSynced(ObjectIdView object_id) override;

  void AddObjectFromSync(ObjectIdView object_id,
                         mx::datapipe_consumer data,
                         size_t size,
                         const std::function<void(Status)>& callback) override;

  void AddObjectFromLocal(
      mx::datapipe_consumer data,
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

  Status SetSyncMetadata(ftl::StringView sync_state) override;

  Status GetSyncMetadata(std::string* sync_state) override;
};

}  // namespace test
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_TEST_PAGE_STORAGE_EMPTY_IMPL_H_
