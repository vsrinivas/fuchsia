// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_RANDOM_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_RANDOM_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/lib/rng/random.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// Implementaton of Commit returning random values (fixed for each instance).
class CommitRandomImpl : public CommitEmptyImpl {
 public:
  CommitRandomImpl(ledger::Random* random, ObjectIdentifierFactory* factory);
  ~CommitRandomImpl() override;
  CommitRandomImpl(const CommitRandomImpl& other);
  CommitRandomImpl& operator=(const CommitRandomImpl& other);

  // Commit:
  std::unique_ptr<const Commit> Clone() const override;

  const CommitId& GetId() const override;

  std::vector<CommitIdView> GetParentIds() const override;

  zx::time_utc GetTimestamp() const override;

  uint64_t GetGeneration() const override;

  ObjectIdentifier GetRootIdentifier() const override;

  absl::string_view GetStorageBytes() const override;

 private:
  CommitId id_;
  zx::time_utc timestamp_;
  uint64_t generation_;
  ObjectIdentifier root_node_identifier_;
  std::vector<CommitId> parent_ids_;
  std::vector<CommitIdView> parent_ids_views_;
  std::string storage_bytes_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_COMMIT_RANDOM_IMPL_H_
