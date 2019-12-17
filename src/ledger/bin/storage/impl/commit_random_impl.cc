// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_random_impl.h"

#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/lib/rng/random.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

CommitRandomImpl::CommitRandomImpl(ledger::Random* random, ObjectIdentifierFactory* factory)
    : id_(RandomCommitId(random)),
      timestamp_(random->Draw<zx::time_utc>()),
      generation_(random->Draw<uint64_t>()),
      root_node_identifier_(RandomObjectIdentifier(random, factory)),
      parent_ids_{RandomCommitId(random)},
      parent_ids_views_{parent_ids_[0]},
      storage_bytes_(RandomString(random, 64)) {}

CommitRandomImpl::~CommitRandomImpl() = default;

CommitRandomImpl::CommitRandomImpl(const CommitRandomImpl& other) { *this = other; }

CommitRandomImpl& CommitRandomImpl::operator=(const CommitRandomImpl& other) {
  id_ = other.id_;
  timestamp_ = other.timestamp_;
  generation_ = other.generation_;
  root_node_identifier_ = other.root_node_identifier_;
  parent_ids_ = other.parent_ids_;
  storage_bytes_ = other.storage_bytes_;
  parent_ids_views_ = {parent_ids_[0]};
  return *this;
}

std::unique_ptr<const Commit> CommitRandomImpl::Clone() const {
  return std::make_unique<CommitRandomImpl>(*this);
}

const CommitId& CommitRandomImpl::GetId() const { return id_; }

std::vector<CommitIdView> CommitRandomImpl::GetParentIds() const { return parent_ids_views_; }

zx::time_utc CommitRandomImpl::GetTimestamp() const { return timestamp_; }

uint64_t CommitRandomImpl::GetGeneration() const { return generation_; }

ObjectIdentifier CommitRandomImpl::GetRootIdentifier() const { return root_node_identifier_; }

absl::string_view CommitRandomImpl::GetStorageBytes() const { return storage_bytes_; }

}  // namespace storage
