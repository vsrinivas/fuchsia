// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TEST_TEST_COMMIT_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TEST_TEST_COMMIT_H_

#include <memory>
#include <vector>

#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/test/commit_empty_impl.h"

namespace cloud_sync {
namespace test {

// Fake implementation of storage::Commit.
class TestCommit : public storage::test::CommitEmptyImpl {
 public:
  TestCommit() = default;
  TestCommit(storage::CommitId id, std::string content);
  ~TestCommit() override = default;

  std::vector<std::unique_ptr<const Commit>> AsList();

  std::unique_ptr<Commit> Clone() const override;

  const storage::CommitId& GetId() const override;

  fxl::StringView GetStorageBytes() const override;

  storage::CommitId id;
  std::string content;
};

}  // namespace test
}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TEST_TEST_COMMIT_H_
