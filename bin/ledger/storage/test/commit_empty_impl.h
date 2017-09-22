// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_EMPTY_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "peridot/bin/ledger/storage/public/commit.h"

namespace storage {
namespace test {

// Empty implementaton of Commit. All methods do nothing and return dummy or
// empty responses.
class CommitEmptyImpl : public Commit {
 public:
  CommitEmptyImpl() = default;
  ~CommitEmptyImpl() override = default;

  // Commit:
  std::unique_ptr<Commit> Clone() const override;

  const CommitId& GetId() const override;

  std::vector<CommitIdView> GetParentIds() const override;

  int64_t GetTimestamp() const override;

  uint64_t GetGeneration() const override;

  ObjectIdView GetRootId() const override;

  fxl::StringView GetStorageBytes() const override;
};

}  // namespace test
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_EMPTY_IMPL_H_
