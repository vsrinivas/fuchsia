// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"

#include <string>
#include <utility>
#include <vector>

#include "peridot/bin/ledger/glue/crypto/hash.h"
#include "peridot/bin/ledger/glue/crypto/rand.h"
#include "peridot/bin/ledger/storage/fake/fake_commit.h"
#include "peridot/bin/ledger/storage/fake/fake_journal.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace storage {
namespace fake {
namespace {

class FakeObject : public Object {
 public:
  FakeObject(ObjectIdView id, fxl::StringView content)
      : id_(id.ToString()), content_(content.ToString()) {}
  ~FakeObject() override {}
  ObjectId GetId() const override { return id_; }
  Status GetData(fxl::StringView* data) const override {
    *data = content_;
    return Status::OK;
  }

 private:
  ObjectId id_;
  std::string content_;
};

storage::ObjectId ComputeObjectId(fxl::StringView value) {
  return glue::SHA256Hash(value);
}

}  // namespace

FakePageStorage::FakePageStorage(PageId page_id)
    : rng_(0), page_id_(std::move(page_id)) {}

FakePageStorage::~FakePageStorage() {}

PageId FakePageStorage::GetId() {
  return page_id_;
}

void FakePageStorage::GetHeadCommitIds(
    std::function<void(Status, std::vector<CommitId>)> callback) {
  std::vector<CommitId> commit_ids;
  for (auto it = journals_.rbegin(); it != journals_.rend(); ++it) {
    if (it->second->IsCommitted()) {
      commit_ids.push_back(it->second->GetId());
      break;
    }
  }
  if (commit_ids.empty()) {
    commit_ids.emplace_back();
  }
  callback(Status::OK, std::move(commit_ids));
}

void FakePageStorage::GetCommit(
    CommitIdView commit_id,
    std::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  auto it = journals_.find(commit_id.ToString());
  if (it == journals_.end()) {
    callback(Status::NOT_FOUND, nullptr);
    return;
  }

  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [
        this, commit_id = commit_id.ToString(), callback = std::move(callback)
      ] {
        callback(Status::OK,
                 std::make_unique<FakeCommit>(journals_[commit_id].get()));
      },
      fxl::TimeDelta::FromMilliseconds(5));
}

void FakePageStorage::StartCommit(
    const CommitId& commit_id,
    JournalType /*journal_type*/,
    std::function<void(Status, std::unique_ptr<Journal>)> callback) {
  auto delegate = std::make_unique<FakeJournalDelegate>(commit_id, autocommit_);
  auto journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  callback(Status::OK, std::move(journal));
}

void FakePageStorage::CommitJournal(
    std::unique_ptr<Journal> journal,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  static_cast<FakeJournal*>(journal.get())->Commit(std::move(callback));
}

void FakePageStorage::RollbackJournal(std::unique_ptr<Journal> journal,
                                      std::function<void(Status)> callback) {
  callback(static_cast<FakeJournal*>(journal.get())->Rollback());
}

Status FakePageStorage::AddCommitWatcher(CommitWatcher* /*watcher*/) {
  return Status::OK;
}

Status FakePageStorage::RemoveCommitWatcher(CommitWatcher* /*watcher*/) {
  return Status::OK;
}

void FakePageStorage::AddObjectFromLocal(
    std::unique_ptr<DataSource> data_source,
    std::function<void(Status, ObjectId)> callback) {
  auto value = std::make_unique<std::string>();
  auto data_source_ptr = data_source.get();
  data_source_ptr->Get(fxl::MakeCopyable([
    this, data_source = std::move(data_source), value = std::move(value),
    callback = std::move(callback)
  ](std::unique_ptr<DataSource::DataChunk> chunk,
    DataSource::Status status) mutable {
    if (status == DataSource::Status::ERROR) {
      callback(Status::IO_ERROR, "");
      return;
    }
    auto view = chunk->Get();
    value->append(view.data(), view.size());
    if (status == DataSource::Status::DONE) {
      std::string object_id = ComputeObjectId(*value);
      objects_[object_id] = std::move(*value);
      callback(Status::OK, std::move(object_id));
    }
  }));
}

void FakePageStorage::GetObject(
    ObjectIdView object_id,
    Location /*location*/,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  GetPiece(object_id, std::move(callback));
}

void FakePageStorage::GetPiece(
    ObjectIdView object_id,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  object_requests_.emplace_back([
    this, object_id = object_id.ToString(), callback = std::move(callback)
  ] {
    auto it = objects_.find(object_id);
    if (it == objects_.end()) {
      callback(Status::NOT_FOUND, nullptr);
      return;
    }

    callback(Status::OK, std::make_unique<FakeObject>(object_id, it->second));
  });
  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this] { SendNextObject(); }, fxl::TimeDelta::FromMilliseconds(5));
}

void FakePageStorage::GetCommitContents(const Commit& commit,
                                        std::string min_key,
                                        std::function<bool(Entry)> on_next,
                                        std::function<void(Status)> on_done) {
  FakeJournalDelegate* journal = journals_[commit.GetId()].get();
  if (!journal) {
    on_done(Status::NOT_FOUND);
    return;
  }
  // Get all entries from this journal and its ancestors.
  std::map<std::string, fake::FakeJournalDelegate::Entry,
           convert::StringViewComparator>
      data;
  while (journal) {
    for (const auto& entry : journal->GetData()) {
      if ((min_key.empty() || min_key <= entry.first) &&
          data.find(entry.first) == data.end()) {
        data[entry.first] = entry.second;
      }
    }
    // FakeJournal currently only supports simple commits.
    journal = journals_[journal->GetParentId()].get();
  }

  for (const auto& entry : data) {
    if (!entry.second.deleted) {
      if (!on_next(
              Entry{entry.first, entry.second.value, entry.second.priority})) {
        break;
      }
    }
  }
  on_done(Status::OK);
}

void FakePageStorage::GetEntryFromCommit(
    const Commit& commit,
    std::string key,
    std::function<void(Status, Entry)> callback) {
  FakeJournalDelegate* journal = journals_[commit.GetId()].get();
  if (!journal) {
    callback(Status::NOT_FOUND, Entry());
    return;
  }
  const std::map<std::string, fake::FakeJournalDelegate::Entry,
                 convert::StringViewComparator>& data = journal->GetData();
  if (data.find(key) == data.end()) {
    callback(Status::NOT_FOUND, Entry());
    return;
  }
  const fake::FakeJournalDelegate::Entry& entry = data.at(key);
  callback(Status::OK, Entry{key, entry.value, entry.priority});
}

const std::map<std::string, std::unique_ptr<FakeJournalDelegate>>&
FakePageStorage::GetJournals() const {
  return journals_;
}

const std::map<ObjectId, std::string, convert::StringViewComparator>&
FakePageStorage::GetObjects() const {
  return objects_;
}

void FakePageStorage::SendNextObject() {
  std::uniform_int_distribution<size_t> distribution(
      0u, object_requests_.size() - 1);
  auto it = object_requests_.begin() + distribution(rng_);
  auto closure = std::move(*it);
  object_requests_.erase(it);
  closure();
}

void FakePageStorage::DeleteObjectFromLocal(const ObjectId& object_id) {
  objects_.erase(object_id);
}

}  // namespace fake
}  // namespace storage
