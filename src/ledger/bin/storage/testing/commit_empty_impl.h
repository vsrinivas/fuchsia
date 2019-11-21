// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_TESTING_COMMIT_EMPTY_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_TESTING_COMMIT_EMPTY_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/public/commit.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// Empty implementation of Commit. All methods do nothing and return dummy or empty responses.
class CommitEmptyImpl : public Commit {
 public:
  CommitEmptyImpl() = default;
  ~CommitEmptyImpl() override = default;

  // Commit:
  std::unique_ptr<const Commit> Clone() const override;

  const CommitId& GetId() const override;

  std::vector<CommitIdView> GetParentIds() const override;

  zx::time_utc GetTimestamp() const override;

  uint64_t GetGeneration() const override;

  ObjectIdentifier GetRootIdentifier() const override;

  absl::string_view GetStorageBytes() const override;

  bool IsAlive() const override;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_TESTING_COMMIT_EMPTY_IMPL_H_
