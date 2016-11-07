// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_
#define APPS_LEDGER_SRC_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_

#include <map>
#include <string>
#include <vector>

#include "apps/ledger/src/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace storage {
namespace fake {

class FakePageStorage : public PageStorage {
 public:
  FakePageStorage(PageId page_id);
  ~FakePageStorage() override;

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

  // For testing:
  const std::map<std::string, std::unique_ptr<FakeJournalDelegate>>&
  GetJournals() const;
  const std::map<ObjectId, std::string, convert::StringViewComparator>&
  GetObjects() const;

 private:
  std::map<std::string, std::unique_ptr<FakeJournalDelegate>> journals_;
  std::map<ObjectId, std::string, convert::StringViewComparator> objects_;
  PageId page_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakePageStorage);
};

}  // namespace fake
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_FAKE_FAKE_PAGE_STORAGE_H_
