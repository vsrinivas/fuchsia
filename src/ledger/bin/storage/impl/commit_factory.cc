// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_factory.h"

#include <lib/fit/function.h>
#include <sys/time.h>

#include <algorithm>
#include <utility>

#include "peridot/lib/rng/random.h"
#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/impl/commit_generated.h"
#include "src/ledger/bin/storage/impl/commit_serialization.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/impl/object_identifier_generated.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_counted.h"
#include "src/ledger/lib/memory/ref_ptr.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace storage {

namespace {

// Size of the commit salt in bytes.
constexpr size_t kCommitSaltSize = 32;

// Checks whether the given |storage_bytes| are a valid serialization of a
// commit.
bool CheckValidSerialization(absl::string_view storage_bytes) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(storage_bytes.data()),
                                 storage_bytes.size());

  if (!VerifyCommitStorageBuffer(verifier)) {
    return false;
  }

  const CommitStorage* commit_storage = GetCommitStorage(storage_bytes.data());
  auto parents = commit_storage->parents();
  return parents && parents->size() >= 1 && parents->size() <= 2;
}

std::string SerializeCommit(uint64_t generation, zx::time_utc timestamp,
                            const ObjectIdentifier& root_node_identifier,
                            std::vector<std::unique_ptr<const Commit>> parent_commits,
                            std::string salt) {
  flatbuffers::FlatBufferBuilder builder;

  auto parents_id = builder.CreateVectorOfStructs(
      parent_commits.size(), static_cast<std::function<void(size_t, IdStorage*)>>(
                                 [&parent_commits](size_t i, IdStorage* child_storage) {
                                   *child_storage = *ToIdStorage(parent_commits[i]->GetId());
                                 }));

  auto root_node_storage = ToObjectIdentifierStorage(&builder, root_node_identifier);
  auto storage = CreateCommitStorage(builder, timestamp.get(), generation, root_node_storage,
                                     parents_id, convert::ToFlatBufferVector(&builder, salt));
  builder.Finish(storage);
  return convert::ToString(builder);
}

class SharedStorageBytes : public ledger::RefCountedThreadSafe<SharedStorageBytes> {
 public:
  const std::string& bytes() { return bytes_; }

 private:
  LEDGER_FRIEND_REF_COUNTED_THREAD_SAFE(SharedStorageBytes);
  LEDGER_FRIEND_MAKE_REF_COUNTED(SharedStorageBytes);
  explicit SharedStorageBytes(std::string bytes) : bytes_(std::move(bytes)) {}
  ~SharedStorageBytes() = default;

  std::string bytes_;
};

}  // namespace

class CommitFactory::CommitImpl : public Commit {
 public:
  // Creates a new |CommitImpl| object with the given contents.
  CommitImpl(CommitId id, zx::time_utc timestamp, uint64_t generation,
             ObjectIdentifier root_node_identifier, std::vector<CommitIdView> parent_ids,
             std::string salt, ledger::RefPtr<SharedStorageBytes> storage_bytes,
             ledger::WeakPtr<CommitFactory> factory);

  ~CommitImpl() override;

  // Commit:
  std::unique_ptr<const Commit> Clone() const override;
  const CommitId& GetId() const override;
  std::vector<CommitIdView> GetParentIds() const override;
  zx::time_utc GetTimestamp() const override;
  uint64_t GetGeneration() const override;
  ObjectIdentifier GetRootIdentifier() const override;
  absl::string_view GetStorageBytes() const override;
  bool IsAlive() const override;

 private:
  const CommitId id_;
  const zx::time_utc timestamp_;
  const uint64_t generation_;
  const ObjectIdentifier root_node_identifier_;
  const std::vector<CommitIdView> parent_ids_;
  const std::string salt_;
  const ledger::RefPtr<SharedStorageBytes> storage_bytes_;
  ledger::WeakPtr<CommitFactory> const factory_;
};

CommitFactory::CommitImpl::CommitImpl(CommitId id, zx::time_utc timestamp, uint64_t generation,
                                      ObjectIdentifier root_node_identifier,
                                      std::vector<CommitIdView> parent_ids, std::string salt,
                                      ledger::RefPtr<SharedStorageBytes> storage_bytes,
                                      ledger::WeakPtr<CommitFactory> factory)
    : id_(std::move(id)),
      timestamp_(timestamp),
      generation_(generation),
      root_node_identifier_(std::move(root_node_identifier)),
      parent_ids_(std::move(parent_ids)),
      salt_(std::move(salt)),
      storage_bytes_(std::move(storage_bytes)),
      factory_(std::move(factory)) {
  LEDGER_DCHECK(id_ == kFirstPageCommitId || (!parent_ids_.empty() && parent_ids_.size() <= 2));
  LEDGER_DCHECK((parent_ids_.size() == 1 && !salt_.empty()) ||
                (parent_ids_.size() != 1 && salt_.empty()));
  LEDGER_DCHECK(factory_);
  factory_->RegisterCommit(this);
}

CommitFactory::CommitImpl::~CommitImpl() {
  if (factory_) {
    factory_->UnregisterCommit(this);
  }
}

std::unique_ptr<const Commit> CommitFactory::CommitImpl::Clone() const {
  return std::make_unique<CommitImpl>(id_, timestamp_, generation_, root_node_identifier_,
                                      parent_ids_, salt_, storage_bytes_, factory_);
}

const CommitId& CommitFactory::CommitImpl::GetId() const { return id_; }

std::vector<CommitIdView> CommitFactory::CommitImpl::GetParentIds() const { return parent_ids_; }

zx::time_utc CommitFactory::CommitImpl::GetTimestamp() const { return timestamp_; }

uint64_t CommitFactory::CommitImpl::GetGeneration() const { return generation_; }

ObjectIdentifier CommitFactory::CommitImpl::GetRootIdentifier() const {
  return root_node_identifier_;
}

absl::string_view CommitFactory::CommitImpl::GetStorageBytes() const {
  return storage_bytes_->bytes();
}

bool CommitFactory::CommitImpl::IsAlive() const { return bool(factory_); }

bool CommitFactory::CommitComparator::operator()(const std::unique_ptr<const Commit>& left,
                                                 const std::unique_ptr<const Commit>& right) const {
  return operator()(left.get(), right.get());
}

bool CommitFactory::CommitComparator::operator()(const Commit* left, const Commit* right) const {
  return std::forward_as_tuple(left->GetTimestamp(), left->GetId()) <
         std::forward_as_tuple(right->GetTimestamp(), right->GetId());
}

CommitFactory::CommitFactory(ObjectIdentifierFactory* object_identifier_factory)
    : object_identifier_factory_(object_identifier_factory), weak_factory_(this) {}

CommitFactory::~CommitFactory() = default;

Status CommitFactory::FromStorageBytes(CommitId id, std::string storage_bytes,
                                       std::unique_ptr<const Commit>* commit) {
  LEDGER_DCHECK(id != kFirstPageCommitId);

  if (!CheckValidSerialization(storage_bytes)) {
    return Status::DATA_INTEGRITY_ERROR;
  }

  auto storage_ptr = ledger::MakeRefCounted<SharedStorageBytes>(std::move(storage_bytes));

  const CommitStorage* commit_storage = GetCommitStorage(storage_ptr->bytes().data());

  ObjectIdentifier root_node_identifier =
      ToObjectIdentifier(commit_storage->root_node_id(), object_identifier_factory_);
  std::vector<CommitIdView> parent_ids;

  for (size_t i = 0; i < commit_storage->parents()->size(); ++i) {
    parent_ids.emplace_back(ToCommitIdView(commit_storage->parents()->Get(i)));
  }

  std::string salt = convert::ToString(commit_storage->salt());

  *commit = std::make_unique<CommitImpl>(
      std::move(id), zx::time_utc(commit_storage->timestamp()), commit_storage->generation(),
      std::move(root_node_identifier), parent_ids, std::move(salt), std::move(storage_ptr),
      weak_factory_.GetWeakPtr());
  return Status::OK;
}

std::unique_ptr<const Commit> CommitFactory::FromContentAndParents(
    timekeeper::Clock* clock, rng::Random* random, ObjectIdentifier root_node_identifier,
    std::vector<std::unique_ptr<const Commit>> parent_commits) {
  LEDGER_DCHECK(parent_commits.size() == 1 || parent_commits.size() == 2);

  uint64_t parent_generation = 0;
  for (const auto& commit : parent_commits) {
    LEDGER_DCHECK(commit->IsAlive());
    parent_generation = std::max(parent_generation, commit->GetGeneration());
  }
  uint64_t generation = parent_generation + 1;

  // Sort commit ids for uniqueness.
  std::sort(parent_commits.begin(), parent_commits.end(),
            [](const std::unique_ptr<const Commit>& c1, const std::unique_ptr<const Commit>& c2) {
              return c1->GetId() < c2->GetId();
            });
  // Compute timestamp.
  zx::time_utc timestamp;
  if (parent_commits.size() == 2) {
    timestamp = std::max(parent_commits[0]->GetTimestamp(), parent_commits[1]->GetTimestamp());
  } else {
    zx_status_t status = clock->Now(&timestamp);
    LEDGER_CHECK(status == ZX_OK);
  }
  // Compute salt.
  std::string salt;
  if (parent_commits.size() == 1) {
    salt.resize(kCommitSaltSize);
    random->Draw(&salt);
  }

  std::string storage_bytes =
      SerializeCommit(generation, timestamp, root_node_identifier, std::move(parent_commits), salt);

  CommitId id = storage::ComputeCommitId(storage_bytes);

  std::unique_ptr<const Commit> commit;
  Status status = FromStorageBytes(std::move(id), std::move(storage_bytes), &commit);
  LEDGER_DCHECK(status == Status::OK);
  return commit;
}

void CommitFactory::Empty(PageStorage* page_storage,
                          fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  btree::TreeNode::Empty(
      page_storage, [weak_this = weak_factory_.GetWeakPtr(), callback = std::move(callback)](
                        Status s, ObjectIdentifier root_identifier) {
        if (s != Status::OK) {
          callback(s, nullptr);
          return;
        }

        LEDGER_DCHECK(IsDigestValid(root_identifier.object_digest()));

        auto storage_ptr = ledger::MakeRefCounted<SharedStorageBytes>("");

        auto ptr = std::make_unique<CommitImpl>(
            convert::ToString(kFirstPageCommitId), zx::time_utc(), 0, std::move(root_identifier),
            std::vector<CommitIdView>(), "", std::move(storage_ptr), std::move(weak_this));
        callback(Status::OK, std::move(ptr));
      });
}

void CommitFactory::AddHeads(std::vector<std::unique_ptr<const Commit>> heads) {
  heads_.insert(std::make_move_iterator(heads.begin()), std::make_move_iterator(heads.end()));
}

void CommitFactory::RemoveHeads(const std::vector<CommitId>& commit_ids) {
  for (const auto& commit_id : commit_ids) {
    auto it = std::find_if(
        heads_.begin(), heads_.end(),
        [&commit_id](const std::unique_ptr<const Commit>& p) { return p->GetId() == commit_id; });
    if (it != heads_.end()) {
      heads_.erase(it);
    }
  }
}

std::vector<std::unique_ptr<const Commit>> CommitFactory::GetHeads() const {
  auto result = std::vector<std::unique_ptr<const Commit>>();
  result.reserve(heads_.size());
  std::transform(heads_.begin(), heads_.end(), std::back_inserter(result),
                 [](const auto& p) -> std::unique_ptr<const Commit> { return p->Clone(); });
  return result;
}

void CommitFactory::AddCommitDependencies(CommitIdView commit_id,
                                          std::vector<ObjectIdentifier> root_identifiers) {
  auto [it, created] =
      live_root_identifiers_.try_emplace(convert::ToString(commit_id), std::move(root_identifiers));
  // TODO(https://bugs.llvm.org/show_bug.cgi?id=43440): Remove lint suppression after clang-tidy
  // understands that post-|try_emplace| use of |root_identifiers| is legitimate if no emplacement
  // occurred.
  LEDGER_DCHECK(created || root_identifiers ==
                               live_root_identifiers_[convert::ToString(commit_id)]);  // NOLINT
}

void CommitFactory::RemoveCommitDependencies(CommitIdView commit_id) {
  auto it = live_root_identifiers_.find(convert::ToString(commit_id));
  if (it != live_root_identifiers_.end()) {
    live_root_identifiers_.erase(it);
  }
  // It is valid to try to delete an entry that was not inserted: Commits received from the cloud
  // are always marked as synced, meaning that this method is eventually called, even though
  // |AddCommitDependencies| was never called on them.
}

std::set<ObjectIdentifier> CommitFactory::GetLiveRootIdentifiers() const {
  // This deduplicates identical ObjectIdentifiers.
  std::set<ObjectIdentifier> result;
  for (const auto& [commit_id, root_identifiers] : live_root_identifiers_) {
    for (const auto& root_identifier : root_identifiers) {
      result.insert(root_identifier);
    }
  }
  return result;
}

void CommitFactory::RegisterCommit(Commit* commit) {
  auto result = live_commits_.insert(commit);
  // Verifies that this is indeed a new commit, and not an already known commit.
  LEDGER_DCHECK(result.second);
}

void CommitFactory::UnregisterCommit(Commit* commit) {
  auto erased = live_commits_.erase(commit);
  // Verifies that the commit was registered previously.
  LEDGER_DCHECK(erased != 0);
}

std::vector<std::unique_ptr<const Commit>> CommitFactory::GetLiveCommits() const {
  // This deduplicates identical commits.
  std::set<const Commit*, CommitComparator> live(live_commits_.begin(), live_commits_.end());
  std::vector<std::unique_ptr<const Commit>> commits;
  for (const Commit* commit : live) {
    commits.emplace_back(commit->Clone());
  }
  return commits;
}

}  // namespace storage
