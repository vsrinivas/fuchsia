// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/fake/fake_page_storage.h"

#include <string>
#include <vector>

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/fake/fake_commit.h"
#include "apps/ledger/src/storage/fake/fake_journal.h"
#include "apps/ledger/src/storage/public/constants.h"
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
  Status GetData(ftl::StringView* data) const override {
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

Status FakePageStorage::GetHeadCommitIds(std::vector<CommitId>* commit_ids) {
  commit_ids->clear();

  for (auto it = journals_.rbegin(); it != journals_.rend(); ++it) {
    if (it->second->IsCommitted()) {
      commit_ids->push_back(it->second->GetId());
      break;
    }
  }
  if (commit_ids->size() == 0) {
    commit_ids->push_back(CommitId());
  }
  return Status::OK;
}

Status FakePageStorage::GetCommit(const CommitId& commit_id,
                                  std::unique_ptr<const Commit>* commit) {
  auto it = journals_.find(commit_id);
  if (it == journals_.end()) {
    return Status::NOT_FOUND;
  }
  *commit = std::make_unique<FakeCommit>(journals_[commit_id].get());
  return Status::OK;
}

Status FakePageStorage::StartCommit(const CommitId& commit_id,
                                    JournalType journal_type,
                                    std::unique_ptr<Journal>* journal) {
  auto delegate = std::make_unique<FakeJournalDelegate>();
  *journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  return Status::OK;
}

Status FakePageStorage::AddCommitWatcher(CommitWatcher* watcher) {
  return Status::OK;
}

Status FakePageStorage::RemoveCommitWatcher(CommitWatcher* watcher) {
  return Status::OK;
}

void FakePageStorage::AddObjectFromLocal(
    mx::datapipe_consumer data,
    int64_t size,
    const std::function<void(Status, ObjectId)>& callback) {
  std::string value;
  mtl::BlockingCopyToString(std::move(data), &value);
  if (size >= 0 && value.size() != static_cast<size_t>(size)) {
    callback(Status::IO_ERROR, "");
    return;
  }
  std::string object_id = RandomId();
  objects_[object_id] = value;
  callback(Status::OK, std::move(object_id));
}

void FakePageStorage::GetObject(
    ObjectIdView object_id,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  auto it = objects_.find(object_id);
  if (it == objects_.end()) {
    callback(Status::NOT_FOUND, nullptr);
    return;
  }

  callback(Status::OK, std::make_unique<FakeObject>(object_id, it->second));
}

Status FakePageStorage::GetObjectSynchronous(
    ObjectIdView object_id,
    std::unique_ptr<const Object>* object) {
  auto it = objects_.find(object_id);
  if (it == objects_.end()) {
    return Status::NOT_FOUND;
  }

  *object = std::make_unique<FakeObject>(object_id, it->second);
  return Status::OK;
}

Status FakePageStorage::AddObjectSynchronous(
    convert::ExtendedStringView data,
    std::unique_ptr<const Object>* object) {
  std::string object_id = RandomId();
  objects_[object_id] = data.ToString();
  return GetObjectSynchronous(object_id, object);
}

const std::map<std::string, std::unique_ptr<FakeJournalDelegate>>&
FakePageStorage::GetJournals() const {
  return journals_;
}

const std::map<ObjectId, std::string, convert::StringViewComparator>&
FakePageStorage::GetObjects() const {
  return objects_;
}

}  // namespace fake
}  // namespace storage
