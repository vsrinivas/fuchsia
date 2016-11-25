// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/test/commit_random_impl.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/logging.h"

namespace storage {
namespace test {
namespace {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}
}

CommitRandomImpl::CommitRandomImpl()
    : id_(RandomId(kCommitIdSize)),
      timestamp_(glue::RandUint64()),
      generation_(glue::RandUint64()),
      root_node_id_(RandomId(kObjectIdSize)),
      parent_ids_{RandomId(kObjectIdSize)},
      storage_bytes_(RandomId(64)) {}

CommitRandomImpl::CommitRandomImpl(const CommitRandomImpl& other)
    : id_(other.id_),
      timestamp_(other.timestamp_),
      generation_(other.generation_),
      root_node_id_(other.root_node_id_),
      parent_ids_(other.parent_ids_),
      storage_bytes_(other.storage_bytes_) {}

std::unique_ptr<Commit> CommitRandomImpl::Clone() const {
  return std::unique_ptr<Commit>(new CommitRandomImpl(*this));
}

CommitId CommitRandomImpl::GetId() const {
  return id_;
}

std::vector<CommitId> CommitRandomImpl::GetParentIds() const {
  return parent_ids_;
}

int64_t CommitRandomImpl::GetTimestamp() const {
  return timestamp_;
}

uint64_t CommitRandomImpl::GetGeneration() const {
  return generation_;
}

std::unique_ptr<CommitContents> CommitRandomImpl::GetContents() const {
  FTL_NOTIMPLEMENTED();
  return nullptr;
}

ObjectId CommitRandomImpl::CommitRandomImpl::GetRootId() const {
  return root_node_id_;
}

std::string CommitRandomImpl::GetStorageBytes() const {
  return storage_bytes_;
}

}  // namespace test
}  // namespace storage
