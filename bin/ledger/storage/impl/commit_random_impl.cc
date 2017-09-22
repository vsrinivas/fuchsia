// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/commit_random_impl.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/impl/storage_test_utils.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/fxl/logging.h"

namespace storage {
namespace test {

CommitRandomImpl::CommitRandomImpl()
    : id_(RandomCommitId()),
      timestamp_(glue::RandUint64()),
      generation_(glue::RandUint64()),
      root_node_id_(RandomObjectId()),
      parent_ids_{RandomObjectId()},
      parent_ids_views_{parent_ids_[0]},
      storage_bytes_(RandomString(64)) {}

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

const CommitId& CommitRandomImpl::GetId() const {
  return id_;
}

std::vector<CommitIdView> CommitRandomImpl::GetParentIds() const {
  return parent_ids_views_;
}

int64_t CommitRandomImpl::GetTimestamp() const {
  return timestamp_;
}

uint64_t CommitRandomImpl::GetGeneration() const {
  return generation_;
}

ObjectIdView CommitRandomImpl::GetRootId() const {
  return root_node_id_;
}

fxl::StringView CommitRandomImpl::GetStorageBytes() const {
  return storage_bytes_;
}

}  // namespace test
}  // namespace storage
