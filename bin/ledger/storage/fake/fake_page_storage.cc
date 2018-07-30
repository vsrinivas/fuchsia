// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"

#include <string>
#include <utility>
#include <vector>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fxl/logging.h>
#include <lib/zx/time.h>

#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/fake/fake_commit.h"
#include "peridot/bin/ledger/storage/fake/fake_journal.h"
#include "peridot/bin/ledger/storage/fake/fake_object.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {
namespace fake {

namespace {

storage::ObjectDigest ComputeDigest(fxl::StringView value) {
  return encryption::SHA256WithLengthHash(value);
}

}  // namespace

FakePageStorage::FakePageStorage(PageId page_id)
    : rng_(0),
      dispatcher_(async_get_default_dispatcher()),
      page_id_(std::move(page_id)),
      encryption_service_(async_get_default_dispatcher()) {}

FakePageStorage::FakePageStorage(async_dispatcher_t* dispatcher, PageId page_id)
    : rng_(0),
      dispatcher_(dispatcher),
      page_id_(std::move(page_id)),
      encryption_service_(dispatcher) {}

FakePageStorage::~FakePageStorage() {}

PageId FakePageStorage::GetId() { return page_id_; }

void FakePageStorage::GetHeadCommitIds(
    fit::function<void(Status, std::vector<CommitId>)> callback) {
  std::vector<CommitId> commit_ids(heads_.begin(), heads_.end());
  if (commit_ids.empty()) {
    commit_ids.emplace_back();
  }
  callback(Status::OK, std::move(commit_ids));
}

void FakePageStorage::GetCommit(
    CommitIdView commit_id,
    fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  auto it = journals_.find(commit_id.ToString());
  if (it == journals_.end()) {
    callback(Status::NOT_FOUND, nullptr);
    return;
  }

  async::PostDelayedTask(
      dispatcher_,
      [this, commit_id = commit_id.ToString(), callback = std::move(callback)] {
        callback(Status::OK,
                 std::make_unique<FakeCommit>(journals_[commit_id].get()));
      },
      kFakePageStorageDelay);
}

void FakePageStorage::StartCommit(
    const CommitId& commit_id, JournalType /*journal_type*/,
    fit::function<void(Status, std::unique_ptr<Journal>)> callback) {
  uint64_t next_generation = 0;
  FakeJournalDelegate::Data data;
  if (journals_.find(commit_id) != journals_.end()) {
    next_generation = journals_[commit_id].get()->GetGeneration() + 1;
    data = journals_[commit_id].get()->GetData();
  }
  auto delegate = std::make_unique<FakeJournalDelegate>(
      std::move(data), commit_id, autocommit_, next_generation);
  auto journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  callback(Status::OK, std::move(journal));
}

void FakePageStorage::StartMergeCommit(
    const CommitId& left, const CommitId& right,
    fit::function<void(Status, std::unique_ptr<Journal>)> callback) {
  auto delegate = std::make_unique<FakeJournalDelegate>(
      journals_[left].get()->GetData(), left, right, autocommit_,
      1 + std::max(journals_[left].get()->GetGeneration(),
                   journals_[right].get()->GetGeneration()));
  auto journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  callback(Status::OK, std::move(journal));
}

void FakePageStorage::CommitJournal(
    std::unique_ptr<Journal> journal,
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  static_cast<FakeJournal*>(journal.get())
      ->Commit([this, callback = std::move(callback)](
                   Status status,
                   std::unique_ptr<const storage::Commit> commit) {
        for (const storage::CommitIdView& parent_id : commit->GetParentIds()) {
          auto it = heads_.find(parent_id.ToString());
          if (it != heads_.end()) {
            heads_.erase(it);
          }
        }
        heads_.emplace(commit->GetId());
        if (!drop_commit_notifications_) {
          for (CommitWatcher* watcher : watchers_) {
            async::PostTask(
                dispatcher_, [watcher, commit = commit->Clone()]() mutable {
                  std::vector<std::unique_ptr<const Commit>> commits;
                  commits.push_back(std::move(commit));
                  watcher->OnNewCommits(commits, ChangeSource::LOCAL);
                });
          }
        }
        callback(status, std::move(commit));
      });
}

void FakePageStorage::RollbackJournal(std::unique_ptr<Journal> journal,
                                      fit::function<void(Status)> callback) {
  callback(static_cast<FakeJournal*>(journal.get())->Rollback());
}

Status FakePageStorage::AddCommitWatcher(CommitWatcher* watcher) {
  watchers_.emplace(watcher);
  return Status::OK;
}

Status FakePageStorage::RemoveCommitWatcher(CommitWatcher* watcher) {
  auto it = watchers_.find(watcher);
  if (it != watchers_.end()) {
    watchers_.erase(it);
  }
  return Status::OK;
}

void FakePageStorage::IsSynced(fit::function<void(Status, bool)> callback) {
  callback(Status::OK, is_synced_);
}

void FakePageStorage::AddObjectFromLocal(
    std::unique_ptr<DataSource> data_source,
    fit::function<void(Status, ObjectIdentifier)> callback) {
  auto value = std::make_unique<std::string>();
  auto data_source_ptr = data_source.get();
  data_source_ptr->Get([this, data_source = std::move(data_source),
                        value = std::move(value),
                        callback = std::move(callback)](
                           std::unique_ptr<DataSource::DataChunk> chunk,
                           DataSource::Status status) mutable {
    if (status == DataSource::Status::ERROR) {
      callback(Status::IO_ERROR, {});
      return;
    }
    auto view = chunk->Get();
    value->append(view.data(), view.size());
    if (status == DataSource::Status::DONE) {
      ObjectIdentifier object_identifier =
          encryption_service_.MakeObjectIdentifier(ComputeDigest(*value));
      objects_[object_identifier] = std::move(*value);
      callback(Status::OK, std::move(object_identifier));
    }
  });
}

void FakePageStorage::GetObject(
    ObjectIdentifier object_identifier, Location /*location*/,
    fit::function<void(Status, std::unique_ptr<const Object>)> callback) {
  GetPiece(object_identifier, std::move(callback));
}

void FakePageStorage::GetPiece(
    ObjectIdentifier object_identifier,
    fit::function<void(Status, std::unique_ptr<const Object>)> callback) {
  object_requests_.emplace_back(
      [this, object_identifier = std::move(object_identifier),
       callback = std::move(callback)] {
        auto it = objects_.find(object_identifier);
        if (it == objects_.end()) {
          callback(Status::NOT_FOUND, nullptr);
          return;
        }

        callback(Status::OK,
                 std::make_unique<FakeObject>(object_identifier, it->second));
      });
  async::PostDelayedTask(dispatcher_, [this] { SendNextObject(); },
                         kFakePageStorageDelay);
}

void FakePageStorage::GetCommitContents(const Commit& commit,
                                        std::string min_key,
                                        fit::function<bool(Entry)> on_next,
                                        fit::function<void(Status)> on_done) {
  FakeJournalDelegate* journal = journals_[commit.GetId()].get();
  if (!journal) {
    on_done(Status::NOT_FOUND);
    return;
  }

  for (auto it = journal->GetData().lower_bound(min_key);
       it != journal->GetData().end(); ++it) {
    if (!on_next(it->second)) {
      break;
    }
  }
  on_done(Status::OK);
}

void FakePageStorage::GetEntryFromCommit(
    const Commit& commit, std::string key,
    fit::function<void(Status, Entry)> callback) {
  FakeJournalDelegate* journal = journals_[commit.GetId()].get();
  if (!journal) {
    callback(Status::NOT_FOUND, Entry());
    return;
  }
  const fake::FakeJournalDelegate::Data& data = journal->GetData();
  auto it = data.find(key);
  if (it == data.end()) {
    callback(Status::NOT_FOUND, Entry());
    return;
  }
  callback(Status::OK, it->second);
}

const std::map<std::string, std::unique_ptr<FakeJournalDelegate>>&
FakePageStorage::GetJournals() const {
  return journals_;
}

const std::map<ObjectIdentifier, std::string>& FakePageStorage::GetObjects()
    const {
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

void FakePageStorage::DeleteObjectFromLocal(
    const ObjectIdentifier& object_identifier) {
  objects_.erase(object_identifier);
}

void FakePageStorage::SetDropCommitNotifications(bool drop) {
  drop_commit_notifications_ = drop;
}

}  // namespace fake
}  // namespace storage
