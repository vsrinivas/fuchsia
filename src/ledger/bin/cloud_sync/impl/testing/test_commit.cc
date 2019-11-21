// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/testing/test_commit.h"

#include <memory>
#include <vector>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {
TestCommit::TestCommit(storage::CommitId id, std::string content)
    : id(std::move(id)), content(std::move(content)) {}

std::vector<std::unique_ptr<const storage::Commit>> TestCommit::AsList() {
  std::vector<std::unique_ptr<const storage::Commit>> result;
  result.push_back(Clone());
  return result;
}

std::unique_ptr<const storage::Commit> TestCommit::Clone() const {
  return std::make_unique<TestCommit>(id, content);
}

const storage::CommitId& TestCommit::GetId() const { return id; }

absl::string_view TestCommit::GetStorageBytes() const { return content; }

}  // namespace cloud_sync
