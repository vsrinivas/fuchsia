// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"

#include <string>
#include <utility>
#include <vector>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>

#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
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
      async_(async_get_default()),
      page_id_(std::move(page_id)),
      encryption_service_(async_get_default()) {}

FakePageStorage::FakePageStorage(async_t* async, PageId page_id)
    : rng_(0),
      async_(async),
      page_id_(std::move(page_id)),
      encryption_service_(async) {}

FakePageStorage::~FakePageStorage() {}

PageId FakePageStorage::GetId() { return page_id_; }

void FakePageStorage::GetHeadCommitIds(
    std::function<void(Status, std::vector<CommitId>)> callback) {
  std::vector<CommitId> commit_ids(heads_.begin(), heads_.end());
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

  async::PostDelayedTask(
      async_,
      [this, commit_id = commit_id.ToString(), callback = std::move(callback)] {
        callback(Status::OK,
                 std::make_unique<FakeCommit>(journals_[commit_id].get()));
      },
      kFakePageStorageDelay);
}

void FakePageStorage::StartCommit(
    const CommitId& commit_id, JournalType /*journal_type*/,
    std::function<void(Status, std::unique_ptr<Journal>)> callback) {
  uint64_t next_generation = 0;
  if (journals_.find(commit_id) != journals_.end()) {
    next_generation = journals_[commit_id].get()->GetGeneration() + 1;
  }
  auto delegate = std::make_unique<FakeJournalDelegate>(commit_id, autocommit_,
                                                        next_generation);
  auto journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  callback(Status::OK, std::move(journal));
}

void FakePageStorage::StartMergeCommit(
    const CommitId& left, const CommitId& right,
    std::function<void(Status, std::unique_ptr<Journal>)> callback) {
  auto delegate = std::make_unique<FakeJournalDelegate>(
      left, right, autocommit_,
      std::max(journals_[left].get()->GetGeneration(),
               journals_[right].get()->GetGeneration()));
  auto journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  callback(Status::OK, std::move(journal));
}

void FakePageStorage::CommitJournal(
    std::unique_ptr<Journal> journal,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
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
                async_, fxl::MakeCopyable([watcher,
                                           commit = commit->Clone()]() mutable {
                  std::vector<std::unique_ptr<const Commit>> commits;
                  commits.push_back(std::move(commit));
                  watcher->OnNewCommits(commits, ChangeSource::LOCAL);
                }));
          }
        }
        callback(status, std::move(commit));
      });
}

void FakePageStorage::RollbackJournal(std::unique_ptr<Journal> journal,
                                      std::function<void(Status)> callback) {
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

void FakePageStorage::AddObjectFromLocal(
    std::unique_ptr<DataSource> data_source,
    std::function<void(Status, ObjectIdentifier)> callback) {
  auto value = std::make_unique<std::string>();
  auto data_source_ptr = data_source.get();
  data_source_ptr->Get(fxl::MakeCopyable(
      [this, data_source = std::move(data_source), value = std::move(value),
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
      }));
}

void FakePageStorage::GetObject(
    ObjectIdentifier object_identifier, Location /*location*/,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  GetPiece(object_identifier, std::move(callback));
}

void FakePageStorage::GetPiece(
    ObjectIdentifier object_identifier,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
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
  async::PostDelayedTask(async_, [this] { SendNextObject(); },
                         kFakePageStorageDelay);
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
    // This only works with simple commits, not merge commits.
    journal = journals_[journal->GetParentIds()[0].ToString()].get();
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
    const Commit& commit, std::string key,
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
