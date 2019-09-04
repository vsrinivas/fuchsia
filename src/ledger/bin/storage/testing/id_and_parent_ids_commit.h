// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_TESTING_ID_AND_PARENT_IDS_COMMIT_H_
#define SRC_LEDGER_BIN_STORAGE_TESTING_ID_AND_PARENT_IDS_COMMIT_H_

#include <set>

#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"

namespace storage {

// An IDs-only implementation of |Commit|.
class IdAndParentIdsCommit : public CommitEmptyImpl {
 public:
  IdAndParentIdsCommit(CommitId id, std::set<CommitId> parents);
  ~IdAndParentIdsCommit() override;

  // Commit
  const CommitId& GetId() const override;
  std::vector<CommitIdView> GetParentIds() const override;

 private:
  CommitId id_;
  std::set<CommitId> parents_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_TESTING_ID_AND_PARENT_IDS_COMMIT_H_
