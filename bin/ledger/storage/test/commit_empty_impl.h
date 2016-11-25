// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_EMPTY_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/commit_contents.h"

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

  std::vector<CommitId> GetParentIds() const override;

  int64_t GetTimestamp() const override;

  std::unique_ptr<CommitContents> GetContents() const override;

  ObjectId GetRootId() const override;

  std::string GetStorageBytes() const override;
};

}  // namespace test
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_EMPTY_IMPL_H_
