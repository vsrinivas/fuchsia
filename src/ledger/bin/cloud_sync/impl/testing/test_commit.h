// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_COMMIT_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_COMMIT_H_

#include <memory>
#include <vector>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {

// Fake implementation of storage::Commit.
class TestCommit : public storage::CommitEmptyImpl {
 public:
  TestCommit() = default;
  TestCommit(storage::CommitId id, std::string content);
  ~TestCommit() override = default;

  std::vector<std::unique_ptr<const Commit>> AsList();

  std::unique_ptr<const Commit> Clone() const override;

  const storage::CommitId& GetId() const override;

  absl::string_view GetStorageBytes() const override;

  storage::CommitId id;
  std::string content;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_COMMIT_H_
