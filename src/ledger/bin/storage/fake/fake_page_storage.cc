// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_page_storage.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <string>
#include <utility>
#include <vector>

#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/fake/fake_commit.h"
#include "src/ledger/bin/storage/fake/fake_journal.h"
#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/socket/strings.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace fake {

namespace {

Status ToBuffer(convert::ExtendedStringView value, int64_t offset, int64_t max_size,
                ledger::SizedVmo* buffer) {
  size_t start = value.size();
  // Valid indices are between -N and N-1.
  if (offset >= -static_cast<int64_t>(value.size()) &&
      offset < static_cast<int64_t>(value.size())) {
    start = offset < 0 ? value.size() + offset : offset;
  }
  size_t length = max_size < 0 ? value.size() : max_size;
  bool result = ledger::VmoFromString(value.substr(start, length), buffer);
  return result ? Status::OK : Status::INTERNAL_ERROR;
}

class FakeRootCommit : public CommitEmptyImpl {
 public:
  FakeRootCommit() : id_(convert::ToString(storage::kFirstPageCommitId)) {}

  std::unique_ptr<const Commit> Clone() const override {
    return std::make_unique<const FakeRootCommit>();
  }

  const CommitId& GetId() const override { return id_; }

  std::vector<CommitIdView> GetParentIds() const override { return std::vector<CommitIdView>(); }

  zx::time_utc GetTimestamp() const override { return zx::time_utc(); }

  uint64_t GetGeneration() const override { return 0; }

 private:
  const CommitId id_;
};

}  // namespace

FakePageStorage::FakePageStorage(ledger::Environment* environment, PageId page_id)
    : page_id_(std::move(page_id)),
      environment_(environment),
      encryption_service_(environment_->dispatcher()) {}

FakePageStorage::~FakePageStorage() = default;

PageId FakePageStorage::GetId() { return page_id_; }

Status FakePageStorage::GetHeadCommits(std::vector<std::unique_ptr<const Commit>>* head_commits) {
  std::vector<std::pair<CommitId, zx::time_utc>> heads(heads_.begin(), heads_.end());
  std::sort(heads.begin(), heads.end(), [](const auto& p1, const auto& p2) {
    return std::tie(p1.second, p1.first) < std::tie(p2.second, p2.first);
  });
  std::vector<std::unique_ptr<const Commit>> commits;
  commits.reserve(heads.size());
  for (const auto& [commit_id, _] : heads) {
    commits.push_back(
        std::make_unique<FakeCommit>(journals_[commit_id].get(), &object_identifier_factory_));
  }
  if (commits.empty()) {
    commits.push_back(std::make_unique<const FakeRootCommit>());
  }
  head_commits->swap(commits);
  return Status::OK;
}

void FakePageStorage::GetMergeCommitIds(
    CommitIdView parent1_id, CommitIdView parent2_id,
    fit::function<void(Status, std::vector<CommitId>)> callback) {
  auto [parent_min_id, parent_max_id] = std::minmax(parent1_id, parent2_id);
  auto it = merges_.find(
      std::make_pair(convert::ToString(parent_min_id), convert::ToString(parent_max_id)));
  auto parents = it != merges_.end() ? it->second : std::vector<CommitId>{};
  callback(Status::OK, std::move(parents));
}

void FakePageStorage::GetCommit(
    CommitIdView commit_id, fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  if (commit_id == storage::kFirstPageCommitId) {
    callback(Status::OK, std::make_unique<const FakeRootCommit>());
    return;
  }
  auto it = journals_.find(convert::ToString(commit_id));
  if (it == journals_.end()) {
    callback(Status::INTERNAL_NOT_FOUND, nullptr);
    return;
  }

  async::PostTask(environment_->dispatcher(),
                  [this, commit_id = convert::ToString(commit_id), callback = std::move(callback)] {
                    callback(Status::OK, std::make_unique<FakeCommit>(journals_[commit_id].get(),
                                                                      &object_identifier_factory_));
                  });
}

std::unique_ptr<Journal> FakePageStorage::StartCommit(std::unique_ptr<const Commit> commit) {
  CommitId commit_id = commit->GetId();
  uint64_t next_generation = 0;
  FakeJournalDelegate::Data data;
  if (journals_.find(commit_id) != journals_.end()) {
    next_generation = journals_[commit_id].get()->GetGeneration() + 1;
    data = journals_[commit_id].get()->GetData();
  }
  auto delegate = std::make_unique<FakeJournalDelegate>(
      environment_->random(), &object_identifier_factory_, std::move(data), commit_id, autocommit_,
      next_generation);
  auto journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  return journal;
}

std::unique_ptr<Journal> FakePageStorage::StartMergeCommit(std::unique_ptr<const Commit> left,
                                                           std::unique_ptr<const Commit> right) {
  const CommitId& left_id = left->GetId();
  const CommitId& right_id = right->GetId();

  auto delegate = std::make_unique<FakeJournalDelegate>(
      environment_->random(), &object_identifier_factory_, journals_[left_id].get()->GetData(),
      left_id, right_id, autocommit_,
      1 + std::max(journals_[left_id].get()->GetGeneration(),
                   journals_[right_id].get()->GetGeneration()));
  auto journal = std::make_unique<FakeJournal>(delegate.get());
  journals_[delegate->GetId()] = std::move(delegate);
  return journal;
}

void FakePageStorage::CommitJournal(
    std::unique_ptr<Journal> journal,
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback) {
  static_cast<FakeJournal*>(journal.get())
      ->Commit([this, callback = std::move(callback)](
                   Status status, std::unique_ptr<const storage::Commit> commit) {
        std::vector<storage::CommitIdView> parent_ids = commit->GetParentIds();
        if (parent_ids.size() == 2) {
          merges_[std::minmax(convert::ToString(parent_ids[0]), convert::ToString(parent_ids[1]))]
              .push_back(commit->GetId());
        }
        for (const storage::CommitIdView& parent_id : parent_ids) {
          auto it = heads_.find(convert::ToString(parent_id));
          if (it != heads_.end()) {
            heads_.erase(it);
          }
        }
        heads_.emplace(commit->GetId(), commit->GetTimestamp());
        if (!drop_commit_notifications_) {
          for (CommitWatcher* watcher : watchers_) {
            async::PostTask(environment_->dispatcher(),
                            [this, watcher, commit = commit->Clone()]() mutable {
                              // Check that watcher was not unregistered.
                              if (watchers_.find(watcher) == watchers_.end()) {
                                return;
                              }
                              std::vector<std::unique_ptr<const Commit>> commits;
                              commits.push_back(std::move(commit));
                              watcher->OnNewCommits(commits, ChangeSource::LOCAL);
                            });
          }
        }
        callback(status, std::move(commit));
      });
}

void FakePageStorage::AddCommitWatcher(CommitWatcher* watcher) { watchers_.emplace(watcher); }

void FakePageStorage::RemoveCommitWatcher(CommitWatcher* watcher) {
  auto it = watchers_.find(watcher);
  if (it != watchers_.end()) {
    watchers_.erase(it);
  }
}

void FakePageStorage::IsSynced(fit::function<void(Status, bool)> callback) {
  callback(Status::OK, is_synced_);
}

void FakePageStorage::AddObjectFromLocal(ObjectType /*object_type*/,
                                         std::unique_ptr<DataSource> data_source,
                                         ObjectReferencesAndPriority tree_references,
                                         fit::function<void(Status, ObjectIdentifier)> callback) {
  auto value = std::make_unique<std::string>();
  auto data_source_ptr = data_source.get();
  data_source_ptr->Get(
      [this, data_source = std::move(data_source), value = std::move(value),
       tree_references = std::move(tree_references), callback = std::move(callback)](
          std::unique_ptr<DataSource::DataChunk> chunk, DataSource::Status status) mutable {
        if (status == DataSource::Status::ERROR) {
          callback(Status::IO_ERROR, {});
          return;
        }
        auto view = chunk->Get();
        value->append(view.data(), view.size());
        if (status == DataSource::Status::DONE) {
          ObjectIdentifier object_identifier = encryption_service_.MakeObjectIdentifier(
              &object_identifier_factory_, FakeDigest(*value));
          objects_[object_identifier] = std::move(*value);
          references_[object_identifier.object_digest()] = std::move(tree_references);
          callback(Status::OK, std::move(object_identifier));
        }
      });
}

void FakePageStorage::GetObject(
    ObjectIdentifier object_identifier, Location /*location*/,
    fit::function<void(Status, std::unique_ptr<const Object>)> callback) {
  GetPiece(object_identifier, [callback = std::move(callback)](Status status,
                                                               std::unique_ptr<const Piece> piece) {
    return callback(status,
                    piece == nullptr ? nullptr : std::make_unique<FakeObject>(std::move(piece)));
  });
}

void FakePageStorage::GetObjectPart(ObjectIdentifier object_identifier, int64_t offset,
                                    int64_t max_size, Location location,
                                    fit::function<void(Status, ledger::SizedVmo)> callback) {
  GetPiece(object_identifier, [offset, max_size, callback = std::move(callback)](
                                  Status status, std::unique_ptr<const Piece> piece) {
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }
    absl::string_view data = piece->GetData();
    ledger::SizedVmo buffer;
    Status buffer_status = ToBuffer(data, offset, max_size, &buffer);
    if (buffer_status != Status::OK) {
      callback(buffer_status, nullptr);
      return;
    }
    callback(Status::OK, std::move(buffer));
  });
}

void FakePageStorage::GetPiece(ObjectIdentifier object_identifier,
                               fit::function<void(Status, std::unique_ptr<const Piece>)> callback) {
  object_requests_.emplace_back(
      [this, object_identifier = std::move(object_identifier), callback = std::move(callback)] {
        auto it = objects_.find(object_identifier);
        if (it == objects_.end()) {
          callback(Status::INTERNAL_NOT_FOUND, nullptr);
          return;
        }

        callback(Status::OK, std::make_unique<FakePiece>(object_identifier, it->second));
      });
  async::PostDelayedTask(
      environment_->dispatcher(), [this] { SendNextObject(); }, kFakePageStorageDelay);
}

void FakePageStorage::GetCommitContents(const Commit& commit, std::string min_key,
                                        fit::function<bool(Entry)> on_next,
                                        fit::function<void(Status)> on_done) {
  FakeJournalDelegate* journal = journals_[commit.GetId()].get();
  if (!journal) {
    on_done(Status::INTERNAL_NOT_FOUND);
    return;
  }

  for (auto it = journal->GetData().lower_bound(min_key); it != journal->GetData().end(); ++it) {
    if (!on_next(it->second)) {
      break;
    }
  }
  on_done(Status::OK);
}

void FakePageStorage::GetEntryFromCommit(const Commit& commit, std::string key,
                                         fit::function<void(Status, Entry)> callback) {
  FakeJournalDelegate* journal = journals_[commit.GetId()].get();
  if (!journal) {
    callback(Status::INTERNAL_NOT_FOUND, Entry());
    return;
  }
  const fake::FakeJournalDelegate::Data& data = journal->GetData();
  auto it = data.find(key);
  if (it == data.end()) {
    callback(Status::INTERNAL_NOT_FOUND, Entry());
    return;
  }
  callback(Status::OK, it->second);
}

const std::map<std::string, std::unique_ptr<FakeJournalDelegate>>& FakePageStorage::GetJournals()
    const {
  return journals_;
}

const std::map<ObjectIdentifier, std::string>& FakePageStorage::GetObjects() const {
  return objects_;
}

const std::map<ObjectDigest, ObjectReferencesAndPriority>& FakePageStorage::GetReferences() const {
  return references_;
}

storage::ObjectIdentifierFactory* FakePageStorage::GetObjectIdentifierFactory() {
  return &object_identifier_factory_;
}

ObjectDigest FakePageStorage::FakeDigest(absl::string_view value) const {
  // Builds a fake ObjectDigest by computing the hash of |value|, and prefixes
  // it with 0xFACEFEED to intentionally make it longer than real object
  // digests, start with a 1 bit, and easy to spot in logs. This is incompatible
  // with real object digests, but is enough for a fake because all clients of
  // the fake should treat object digests as opaque blobs.
  return ObjectDigest(absl::StrCat("\xFA\xCE\xFE\xED", encryption::SHA256WithLengthHash(value)));
}

void FakePageStorage::SendNextObject() {
  auto rng = environment_->random()->NewBitGenerator<uint64_t>();
  std::uniform_int_distribution<size_t> distribution(0u, object_requests_.size() - 1);
  auto it = object_requests_.begin() + distribution(rng);
  auto closure = std::move(*it);
  object_requests_.erase(it);
  closure();
}

void FakePageStorage::DeleteObjectFromLocal(const ObjectIdentifier& object_identifier) {
  objects_.erase(object_identifier);
}

void FakePageStorage::SetDropCommitNotifications(bool drop) { drop_commit_notifications_ = drop; }

}  // namespace fake
}  // namespace storage
