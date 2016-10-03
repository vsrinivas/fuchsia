// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/commit_impl.h"

#include "apps/ledger/storage/public/constants.h"
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

CommitImpl::CommitImpl(const CommitId& id,
                       int64_t timestamp,
                       const ObjectId& root_node_id,
                       const std::vector<CommitId>& parent_ids)
    : id_(id),
      timestamp_(timestamp),
      root_node_id_(root_node_id),
      parent_ids_(parent_ids) {
  FTL_DCHECK(!parent_ids_.empty() && parent_ids_.size() <= 2);
}

CommitImpl::~CommitImpl() {}

std::unique_ptr<Commit> CommitImpl::FromStorageBytes(
    const CommitId& id,
    const std::string& storage_bytes) {
  int parentCount = (storage_bytes.size() - kParentsStartIndex) / kCommitIdSize;

  if ((storage_bytes.size() - kParentsStartIndex) % kCommitIdSize != 0 ||
      parentCount < 1 || parentCount > 2) {
    FTL_LOG(ERROR) << "Illegal format for commit storage bytes "
                   << storage_bytes;
    return nullptr;
  }

  int64_t timestamp = BytesToTimestamp(
      storage_bytes.substr(kTimestampStartIndex, kTimestampSize));
  ObjectId rootNodeId =
      storage_bytes.substr(kRootNodeStartIndex, kObjectIdSize);
  std::vector<CommitId> parentIds;

  for (int i = 0; i < parentCount; i++) {
    parentIds.push_back(storage_bytes.substr(
        kParentsStartIndex + i * kCommitIdSize, kCommitIdSize));
  }
  return std::unique_ptr<Commit>(
      new CommitImpl(id, timestamp, rootNodeId, parentIds));
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
  FTL_LOG(ERROR) << "Not implemented yet.";
  return nullptr;
}

std::string CommitImpl::GetStorageBytes() const {
  std::string result;
  result.reserve(kTimestampSize + kObjectIdSize +
                 parent_ids_.size() * kCommitIdSize);
  result.append(TimestampToBytes(timestamp_))
      .append(root_node_id_)
      .append(parent_ids_[0]);
  return (parent_ids_.size() == 1 ? result : result.append(parent_ids_[1]));
}

}  // namespace storage
