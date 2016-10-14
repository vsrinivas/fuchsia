// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/fake/fake_page_storage.h"

#include <string>
#include <vector>

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/fake/fake_journal.h"
#include "apps/ledger/storage/public/constants.h"
#include "lib/mtl/data_pipe/strings.h"

namespace storage {
namespace fake {
namespace {

class FakeObject : public Object {
 public:
  FakeObject(ObjectIdView id, ftl::StringView content)
      : id_(id.ToString()), content_(content.ToString()) {}
  ~FakeObject() {}
  ObjectId GetId() const override { return id_; }
  Status GetData(ftl::StringView* data) override {
    *data = content_;
    return Status::OK;
  }

 private:
  ObjectId id_;
  std::string content_;
};

storage::ObjectId RandomId() {
  std::string result;
  result.resize(kObjectIdSize);
  glue::RandBytes(&result[0], kObjectIdSize);
  return result;
}

}  // namespace

FakePageStorage::FakePageStorage(PageId page_id) : page_id_(page_id) {}

FakePageStorage::~FakePageStorage() {}

PageId FakePageStorage::GetId() {
  return page_id_;
}

void FakePageStorage::SetPageDeletionHandler(
    const std::function<void()>& on_page_deletion) {}

Status FakePageStorage::GetHeadCommitIds(std::vector<CommitId>* commit_ids) {
  commit_ids->clear();

  for (auto it = journals_.begin(); it < journals_.end(); ++it) {
    if ((*it)->IsCommitted()) {
      commit_ids->push_back((*it)->GetId());
    }
  }
  if (commit_ids->size() == 0) {
    commit_ids->push_back(CommitId());
  }
  return Status::OK;
}

Status FakePageStorage::GetCommit(const CommitId& commit_id,
                                  std::unique_ptr<Commit>* commit) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::AddCommitFromSync(const CommitId& id,
                                          const std::string& storage_bytes) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::StartCommit(const CommitId& commit_id,
                                    JournalType journal_type,
                                    std::unique_ptr<Journal>* journal) {
  std::unique_ptr<FakeJournalDelegate> delegate(new FakeJournalDelegate);
  std::unique_ptr<Journal> fake_journal(new FakeJournal(delegate.get()));
  journal->swap(fake_journal);
  journals_.push_back(std::move(delegate));
  return Status::OK;
}

Status FakePageStorage::StartMergeCommit(const CommitId& left,
                                         const CommitId& right,
                                         std::unique_ptr<Journal>* journal) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::AddCommitWatcher(CommitWatcher* watcher) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::RemoveCommitWatcher(CommitWatcher* watcher) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::GetUnsyncedCommits(
    std::vector<std::unique_ptr<Commit>>* commits) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::MarkCommitSynced(const CommitId& commit_id) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::GetDeltaObjects(const CommitId& commit_id,
                                        std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::GetUnsyncedObjects(const CommitId& commit_id,
                                           std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::MarkObjectSynced(ObjectIdView object_id) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::AddObjectFromSync(
    ObjectIdView object_id,
    mojo::ScopedDataPipeConsumerHandle data,
    size_t size) {
  return Status::NOT_IMPLEMENTED;
}

Status FakePageStorage::AddObjectFromLocal(
    mojo::ScopedDataPipeConsumerHandle data,
    size_t size,
    ObjectId* object_id) {
  std::string value;
  mtl::BlockingCopyToString(std::move(data), &value);
  if (value.size() != size) {
    return Status::ILLEGAL_STATE;
  }
  *object_id = RandomId();
  objects_[*object_id] = value;
  return Status::OK;
}

void FakePageStorage::GetBlob(
    ObjectIdView blob_id,
    const std::function<void(Status status, std::unique_ptr<Blob> blob)>
        callback) {
  auto it = objects_.find(blob_id);
  if (it == objects_.end()) {
    callback(Status::NOT_FOUND, nullptr);
    return;
  }

  callback(Status::OK, std::make_unique<FakeObject>(blob_id, it->second));
}

const std::vector<std::unique_ptr<FakeJournalDelegate>>&
FakePageStorage::GetJournals() const {
  return journals_;
}

const std::map<ObjectId, std::string, convert::StringViewComparator>&
FakePageStorage::GetObjects() const {
  return objects_;
}

}  // namespace fake
}  // namespace storage
