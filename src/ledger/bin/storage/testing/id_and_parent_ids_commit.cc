// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/testing/id_and_parent_ids_commit.h"

#include <set>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

IdAndParentIdsCommit::IdAndParentIdsCommit(CommitId id, std::set<CommitId> parents)
    : id_(std::move(id)), parents_(std::move(parents)) {}

IdAndParentIdsCommit::~IdAndParentIdsCommit() = default;

const CommitId& IdAndParentIdsCommit::GetId() const { return id_; };

std::vector<CommitIdView> IdAndParentIdsCommit::GetParentIds() const {
  std::vector<CommitIdView> parent_views;
  parent_views.reserve(parents_.size());
  for (const CommitId& parent : parents_) {
    parent_views.push_back(parent);
  }
  return parent_views;
};

}  // namespace storage
