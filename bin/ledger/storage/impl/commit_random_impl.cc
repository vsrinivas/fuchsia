// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/commit_random_impl.h"

#include <lib/fxl/logging.h>
#include <lib/fxl/random/rand.h>

#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {

CommitRandomImpl::CommitRandomImpl()
    : id_(RandomCommitId()),
      timestamp_(fxl::RandUint64()),
      generation_(fxl::RandUint64()),
      root_node_identifier_(RandomObjectIdentifier()),
      parent_ids_{RandomObjectDigest()},
      parent_ids_views_{parent_ids_[0]},
      storage_bytes_(RandomString(64)) {}

CommitRandomImpl::CommitRandomImpl(const CommitRandomImpl& other)
    : id_(other.id_),
      timestamp_(other.timestamp_),
      generation_(other.generation_),
      root_node_identifier_(other.root_node_identifier_),
      parent_ids_(other.parent_ids_),
      storage_bytes_(other.storage_bytes_) {}

std::unique_ptr<Commit> CommitRandomImpl::Clone() const {
  return std::unique_ptr<Commit>(new CommitRandomImpl(*this));
}

const CommitId& CommitRandomImpl::GetId() const { return id_; }

std::vector<CommitIdView> CommitRandomImpl::GetParentIds() const {
  return parent_ids_views_;
}

int64_t CommitRandomImpl::GetTimestamp() const { return timestamp_; }

uint64_t CommitRandomImpl::GetGeneration() const { return generation_; }

ObjectIdentifier CommitRandomImpl::GetRootIdentifier() const {
  return root_node_identifier_;
}

fxl::StringView CommitRandomImpl::GetStorageBytes() const {
  return storage_bytes_;
}

}  // namespace storage
