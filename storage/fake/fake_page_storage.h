// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_
#define APPS_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "apps/ledger/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "lib/ftl/macros.h"

namespace storage {
namespace fake {

class FakePageStorage : public PageStorage {
 public:
  FakePageStorage(PageId page_id);
  ~FakePageStorage() override;

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

  // For testing:
  const std::vector<std::unique_ptr<FakeJournalDelegate>>& GetJournals() const;
  const std::unordered_map<ObjectId, std::string>& GetObjects() const;

 private:
  std::vector<std::unique_ptr<FakeJournalDelegate>> journals_;
  std::unordered_map<ObjectId, std::string> objects_;
  PageId page_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakePageStorage);
};

}  // namespace fake
}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_
