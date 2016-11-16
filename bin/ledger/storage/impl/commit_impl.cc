// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/commit_impl.h"

#include <algorithm>

#include <sys/time.h>

#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/storage/impl/btree/commit_contents_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/build_config.h"
#include "lib/ftl/logging.h"

#if !defined(ARCH_CPU_LITTLE_ENDIAN)
#error "Big endian is not supported"
#endif

namespace storage {

namespace {

const int kTimestampSize = 8;

const int kTimestampStartIndex = 0;
const int kRootNodeStartIndex = kTimestampStartIndex + kTimestampSize;
const int kParentsStartIndex = kRootNodeStartIndex + kObjectIdSize;

std::string TimestampToBytes(int64_t timestamp) {
  static_assert(sizeof(timestamp) == kTimestampSize, "Illegal timestamp size");
  return std::string(reinterpret_cast<char*>(&timestamp), kTimestampSize);
}

int64_t BytesToTimestamp(std::string bytes) {
  return *reinterpret_cast<const uint64_t*>(bytes.data());
}

}  // namespace

CommitImpl::CommitImpl(PageStorage* page_storage,
                       const CommitId& id,
                       int64_t timestamp,
                       ObjectIdView root_node_id,
                       const std::vector<CommitId>& parent_ids,
                       std::string&& storage_bytes)
    : page_storage_(page_storage),
      id_(id),
      timestamp_(timestamp),
      root_node_id_(root_node_id.ToString()),
      parent_ids_(parent_ids),
      storage_bytes_(std::move(storage_bytes)) {
  FTL_DCHECK(page_storage_ != nullptr);
  FTL_DCHECK(id == kFirstPageCommitId ||
             (!parent_ids_.empty() && parent_ids_.size() <= 2));
}

CommitImpl::~CommitImpl() {}

std::unique_ptr<Commit> CommitImpl::FromStorageBytes(
    PageStorage* page_storage,
    const CommitId& id,
    std::string&& storage_bytes) {
  int parent_count =
      (storage_bytes.size() - kParentsStartIndex) / kCommitIdSize;

  if ((storage_bytes.size() - kParentsStartIndex) % kCommitIdSize != 0 ||
      parent_count < 1 || parent_count > 2) {
    FTL_LOG(ERROR) << "Illegal format for commit storage bytes "
                   << storage_bytes;
    return nullptr;
  }

  int64_t timestamp = BytesToTimestamp(
      storage_bytes.substr(kTimestampStartIndex, kTimestampSize));
  ObjectId root_node_id =
      storage_bytes.substr(kRootNodeStartIndex, kObjectIdSize);
  std::vector<CommitId> parent_ids;

  for (int i = 0; i < parent_count; i++) {
    parent_ids.push_back(storage_bytes.substr(
        kParentsStartIndex + i * kCommitIdSize, kCommitIdSize));
  }
  return std::unique_ptr<Commit>(new CommitImpl(page_storage, id, timestamp,
                                                root_node_id, parent_ids,
                                                std::move(storage_bytes)));
}

std::unique_ptr<Commit> CommitImpl::FromContentAndParents(
    PageStorage* page_storage,
    ObjectIdView root_node_id,
    std::vector<CommitId>&& parent_ids) {
  // Sort commit ids for uniqueness.
  std::sort(parent_ids.begin(), parent_ids.end());
  // Compute timestamp.
  struct timeval tv;
  gettimeofday(&tv, NULL);
  int64_t timestamp = static_cast<int64_t>(tv.tv_sec) * 1000000000L +
                      static_cast<int64_t>(tv.tv_usec) * 1000L;

  std::string storage_bytes;
  storage_bytes.reserve(kTimestampSize + kObjectIdSize +
                        parent_ids.size() * kCommitIdSize);
  storage_bytes.append(TimestampToBytes(timestamp))
      .append(root_node_id.data(), root_node_id.size())
      .append(parent_ids[0]);
  if (parent_ids.size() != 1)
    storage_bytes.append(parent_ids[1]);
  CommitId id = glue::SHA256Hash(storage_bytes.data(), storage_bytes.size());

  return std::unique_ptr<Commit>(
      new CommitImpl(page_storage, id, timestamp, root_node_id,
                     std::move(parent_ids), std::move(storage_bytes)));
}

std::unique_ptr<Commit> CommitImpl::Empty(PageStorage* page_storage) {
  ObjectId root_node_id;
  TreeNode::FromEntries(page_storage, std::vector<Entry>(),
                        std::vector<ObjectId>(1), &root_node_id);
  return std::unique_ptr<Commit>(
      new CommitImpl(page_storage, kFirstPageCommitId, 0, root_node_id,
                     std::vector<CommitId>(), ""));
}

std::unique_ptr<Commit> CommitImpl::Clone() const {
  return std::unique_ptr<CommitImpl>(
      new CommitImpl(page_storage_, id_, timestamp_, root_node_id_, parent_ids_,
                     std::string(storage_bytes_)));
}

CommitId CommitImpl::GetId() const {
  return id_;
}

std::vector<CommitId> CommitImpl::GetParentIds() const {
  return parent_ids_;
}

int64_t CommitImpl::GetTimestamp() const {
  return timestamp_;
}

std::unique_ptr<CommitContents> CommitImpl::GetContents() const {
  return std::make_unique<CommitContentsImpl>(root_node_id_, page_storage_);
}

ObjectId CommitImpl::CommitImpl::GetRootId() const {
  return root_node_id_;
}

std::string CommitImpl::GetStorageBytes() const {
  return storage_bytes_;
}

}  // namespace storage
