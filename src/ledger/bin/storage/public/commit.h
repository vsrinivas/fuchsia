// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_COMMIT_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_COMMIT_H_

#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/public/types.h"

namespace storage {

class Commit {
 public:
  Commit() = default;
  Commit(const Commit&) = delete;
  Commit& operator=(const Commit&) = delete;
  virtual ~Commit() = default;

  // Returns a copy of the commit.
  virtual std::unique_ptr<const Commit> Clone() const = 0;

  // Returns the id of this commit.
  virtual const CommitId& GetId() const = 0;

  // Returns the ids of this commit's parents.
  virtual std::vector<CommitIdView> GetParentIds() const = 0;

  // Returns the creation timestamp of this commit in nanoseconds since epoch.
  virtual zx::time_utc GetTimestamp() const = 0;

  // Returns the generation of this commit (ie. the number of commits to the
  // root).
  virtual uint64_t GetGeneration() const = 0;

  // Returns the id of the root node of this commit.
  virtual ObjectIdentifier GetRootIdentifier() const = 0;

  // Returns the byte representation of this |Commit| as they will be synced to
  // the cloud.
  virtual fxl::StringView GetStorageBytes() const = 0;

  static bool TimestampOrdered(const std::unique_ptr<const Commit>& commit1,
                               const std::unique_ptr<const Commit>& commit2);

  // Returns true if new commits can use this commit object as parent. False otherwise.
  virtual bool IsAlive() const = 0;
};

// Generate an id for a commit based on its content.
std::string ComputeCommitId(fxl::StringView content);

// Comparator for commits that order commits based on their generation, then on
// their id, with highest generation/highest id first.
struct GenerationComparator {
  bool operator()(const std::unique_ptr<const storage::Commit>& lhs,
                  const std::unique_ptr<const storage::Commit>& rhs) const;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_COMMIT_H_
