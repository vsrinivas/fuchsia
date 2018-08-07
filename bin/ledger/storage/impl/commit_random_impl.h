// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_COMMIT_RANDOM_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_COMMIT_RANDOM_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "peridot/bin/ledger/storage/public/commit.h"

namespace storage {

// Implementaton of Commit returning random values (fixed for each instance).
class CommitRandomImpl : public Commit {
 public:
  CommitRandomImpl();
  ~CommitRandomImpl() override = default;

  // Commit:
  std::unique_ptr<Commit> Clone() const override;

  const CommitId& GetId() const override;

  std::vector<CommitIdView> GetParentIds() const override;

  int64_t GetTimestamp() const override;

  uint64_t GetGeneration() const override;

  ObjectIdentifier GetRootIdentifier() const override;

  fxl::StringView GetStorageBytes() const override;

 private:
  CommitRandomImpl(const CommitRandomImpl& other);

  CommitId id_;
  int64_t timestamp_;
  uint64_t generation_;
  ObjectIdentifier root_node_identifier_;
  std::vector<CommitId> parent_ids_;
  std::vector<CommitIdView> parent_ids_views_;
  std::string storage_bytes_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_COMMIT_RANDOM_IMPL_H_
