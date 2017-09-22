// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/test/test_commit.h"

#include <memory>
#include <vector>

#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/test/commit_empty_impl.h"

namespace cloud_sync {
namespace test {
TestCommit::TestCommit(storage::CommitId id, std::string content)
    : id(std::move(id)), content(std::move(content)) {}

std::vector<std::unique_ptr<const storage::Commit>> TestCommit::AsList() {
  std::vector<std::unique_ptr<const storage::Commit>> result;
  result.push_back(Clone());
  return result;
}

std::unique_ptr<storage::Commit> TestCommit::Clone() const {
  return std::make_unique<TestCommit>(id, content);
}

const storage::CommitId& TestCommit::GetId() const {
  return id;
}

fxl::StringView TestCommit::GetStorageBytes() const {
  return content;
}

}  // namespace test
}  // namespace cloud_sync
