// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_

#include "apps/ledger/storage/public/page_storage.h"

namespace storage {

class PageStorageImpl : public PageStorage {
 public:
  PageStorageImpl(std::string page_path, PageId page_id);
  ~PageStorageImpl() override;

  // PageStorage:
  PageId GetId() override;
  void SetPageDeletionHandler(
      const std::function<void()>& on_page_deletion) override;
  Status GetHeadCommitIds(std::vector<CommitId>* commit_ids) override;
  Status GetCommit(const CommitId& commit_id,
                   std::unique_ptr<Commit>* commit) override;
  Status AddCommitFromSync(const CommitId& id,
                           const std::string& storage_bytes) override;
  Status StartCommit(const CommitId& commit_id,
                     bool implicit,
                     std::unique_ptr<Journal>* journal) override;
  Status StartMergeCommit(const CommitId& left,
                          const CommitId& right,
                          std::unique_ptr<Journal>* journal) override;
  Status AddCommitWatcher(CommitWatcher* watcher) override;
  Status RemoveCommitWatcher(CommitWatcher* watcher) override;
  Status GetUnsyncedCommits(
      std::vector<std::unique_ptr<Commit>>* commits) override;
  Status MarkCommitSynced(const CommitId& commit_id) override;
  Status GetDeltaObjects(const CommitId& commit_id,
                         std::vector<Object>* objects) override;
  Status GetUnsyncedObjects(const CommitId& commit_id,
                            std::vector<Object>* objects) override;
  Status MarkObjectSynced(const ObjectId& object_id) override;
  Status AddObjectFromSync(const ObjectId& object_id,
                           mojo::DataPipeConsumerHandle data,
                           size_t size) override;
  Status AddObjectFromLocal(mojo::DataPipeConsumerHandle data,
                            size_t size,
                            ObjectId* object_id) override;
  void GetBlob(
      const ObjectId& blob_id,
      const std::function<void(Status status, std::unique_ptr<Blob> blob)>
          callback) override;

 private:
  std::string page_path_;
  PageId page_id_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_PAGE_STORAGE_IMPL_H_
