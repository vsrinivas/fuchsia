// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/commit_impl.h"

#include <sys/time.h>
#include <algorithm>
#include <utility>

#include <flatbuffers/flatbuffers.h>

#include "lib/fxl/build_config.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/ref_counted.h"
#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"
#include "peridot/bin/ledger/storage/impl/commit_generated.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"
#include "peridot/bin/ledger/storage/impl/object_identifier_generated.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {

namespace {

static_assert(sizeof(IdStorage) == kCommitIdSize,
              "storage size for id is incorrect");

const IdStorage* ToIdStorage(CommitIdView id) {
  return reinterpret_cast<const IdStorage*>(id.data());
}

CommitIdView ToCommitIdView(const IdStorage* id_storage) {
  return CommitIdView(fxl::StringView(reinterpret_cast<const char*>(id_storage),
                                      sizeof(IdStorage)));
}

}  // namespace

class CommitImpl::SharedStorageBytes
    : public fxl::RefCountedThreadSafe<SharedStorageBytes> {
 public:
  const std::string& bytes() { return bytes_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(SharedStorageBytes);
  FRIEND_MAKE_REF_COUNTED(SharedStorageBytes);
  explicit SharedStorageBytes(std::string bytes) : bytes_(std::move(bytes)) {}
  ~SharedStorageBytes() {}

  std::string bytes_;
};

namespace {
// Checks whether the given |storage_bytes| are a valid serialization of a
// commit.
bool CheckValidSerialization(fxl::StringView storage_bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(storage_bytes.data()),
      storage_bytes.size());

  if (!VerifyCommitStorageBuffer(verifier)) {
    return false;
  };

  const CommitStorage* commit_storage = GetCommitStorage(storage_bytes.data());
  auto parents = commit_storage->parents();
  return parents && parents->size() >= 1 && parents->size() <= 2;
}

std::string SerializeCommit(
    uint64_t generation, int64_t timestamp,
    const ObjectIdentifier& root_node_identifier,
    std::vector<std::unique_ptr<const Commit>> parent_commits) {
  flatbuffers::FlatBufferBuilder builder;

  auto parents_id = builder.CreateVectorOfStructs(
      parent_commits.size(),
      static_cast<std::function<void(size_t, IdStorage*)>>(
          [&parent_commits](size_t i, IdStorage* child_storage) {
            *child_storage = *ToIdStorage(parent_commits[i]->GetId());
          }));

  auto root_node_storage =
      ToObjectIdentifierStorage(&builder, root_node_identifier);
  auto storage = CreateCommitStorage(builder, timestamp, generation,
                                     root_node_storage, parents_id);
  builder.Finish(storage);
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                     builder.GetSize());
}
}  // namespace

CommitImpl::CommitImpl(Token /* token */, PageStorage* page_storage,
                       CommitId id, int64_t timestamp, uint64_t generation,
                       ObjectIdentifier root_node_identifier,
                       std::vector<CommitIdView> parent_ids,
                       fxl::RefPtr<SharedStorageBytes> storage_bytes)
    : page_storage_(page_storage),
      id_(std::move(id)),
      timestamp_(timestamp),
      generation_(generation),
      root_node_identifier_(std::move(root_node_identifier)),
      parent_ids_(std::move(parent_ids)),
      storage_bytes_(std::move(storage_bytes)) {
  FXL_DCHECK(page_storage_ != nullptr);
  FXL_DCHECK(id_ == kFirstPageCommitId ||
             (!parent_ids_.empty() && parent_ids_.size() <= 2));
}

CommitImpl::~CommitImpl() {}

Status CommitImpl::FromStorageBytes(PageStorage* page_storage, CommitId id,
                                    std::string storage_bytes,
                                    std::unique_ptr<const Commit>* commit) {
  FXL_DCHECK(id != kFirstPageCommitId);

  if (!CheckValidSerialization(storage_bytes)) {
    return Status::FORMAT_ERROR;
  }

  auto storage_ptr =
      fxl::MakeRefCounted<SharedStorageBytes>(std::move(storage_bytes));

  const CommitStorage* commit_storage =
      GetCommitStorage(storage_ptr->bytes().data());

  ObjectIdentifier root_node_identifier =
      ToObjectIdentifier(commit_storage->root_node_id());
  std::vector<CommitIdView> parent_ids;

  for (size_t i = 0; i < commit_storage->parents()->size(); ++i) {
    parent_ids.emplace_back(ToCommitIdView(commit_storage->parents()->Get(i)));
  }
  *commit = std::make_unique<CommitImpl>(
      Token(), page_storage, std::move(id), commit_storage->timestamp(),
      commit_storage->generation(), std::move(root_node_identifier), parent_ids,
      std::move(storage_ptr));
  return Status::OK;
}

std::unique_ptr<const Commit> CommitImpl::FromContentAndParents(
    PageStorage* page_storage, ObjectIdentifier root_node_identifier,
    std::vector<std::unique_ptr<const Commit>> parent_commits) {
  FXL_DCHECK(parent_commits.size() == 1 || parent_commits.size() == 2);

  uint64_t parent_generation = 0;
  for (const auto& commit : parent_commits) {
    parent_generation = std::max(parent_generation, commit->GetGeneration());
  }
  uint64_t generation = parent_generation + 1;

  // Sort commit ids for uniqueness.
  std::sort(parent_commits.begin(), parent_commits.end(),
            [](const std::unique_ptr<const Commit>& c1,
               const std::unique_ptr<const Commit>& c2) {
              return c1->GetId() < c2->GetId();
            });
  // Compute timestamp.
  int64_t timestamp;
  if (parent_commits.size() == 2) {
    timestamp = std::max(parent_commits[0]->GetTimestamp(),
                         parent_commits[1]->GetTimestamp());
  } else {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    timestamp = static_cast<int64_t>(tv.tv_sec) * 1000000000L +
                static_cast<int64_t>(tv.tv_usec) * 1000L;
  }

  std::string storage_bytes = SerializeCommit(
      generation, timestamp, root_node_identifier, std::move(parent_commits));

  CommitId id = encryption::SHA256WithLengthHash(storage_bytes);

  std::unique_ptr<const Commit> commit;
  Status status = FromStorageBytes(page_storage, std::move(id),
                                   std::move(storage_bytes), &commit);
  FXL_DCHECK(status == Status::OK);
  return commit;
}

void CommitImpl::Empty(
    PageStorage* page_storage,
    std::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  btree::TreeNode::Empty(
      page_storage, [page_storage, callback = std::move(callback)](
                        Status s, ObjectIdentifier root_identifier) {
        if (s != Status::OK) {
          callback(s, nullptr);
          return;
        }

        FXL_DCHECK(IsDigestValid(root_identifier.object_digest));

        auto storage_ptr = fxl::MakeRefCounted<SharedStorageBytes>("");

        auto ptr = std::make_unique<CommitImpl>(
            Token(), page_storage, kFirstPageCommitId.ToString(), 0, 0,
            std::move(root_identifier), std::vector<CommitIdView>(),
            std::move(storage_ptr));
        callback(Status::OK, std::move(ptr));
      });
}

std::unique_ptr<Commit> CommitImpl::Clone() const {
  return std::make_unique<CommitImpl>(Token(), page_storage_, id_, timestamp_,
                                      generation_, root_node_identifier_,
                                      parent_ids_, storage_bytes_);
}

const CommitId& CommitImpl::GetId() const { return id_; }

std::vector<CommitIdView> CommitImpl::GetParentIds() const {
  return parent_ids_;
}

int64_t CommitImpl::GetTimestamp() const { return timestamp_; }

uint64_t CommitImpl::GetGeneration() const { return generation_; }

ObjectIdentifier CommitImpl::GetRootIdentifier() const {
  return root_node_identifier_;
}

fxl::StringView CommitImpl::GetStorageBytes() const {
  return storage_bytes_->bytes();
}

}  // namespace storage
