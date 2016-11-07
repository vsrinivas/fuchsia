// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_PUBLIC_COMMIT_H_
#define APPS_LEDGER_SRC_STORAGE_PUBLIC_COMMIT_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/storage/public/commit_contents.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {

class Commit {
 public:
  Commit() {}
  virtual ~Commit() {}

  // Returns the id of this commit.
  virtual CommitId GetId() const = 0;

  // Returns the ids of this commit's parents.
  virtual std::vector<CommitId> GetParentIds() const = 0;

  // Returns the creation timestamp of this commit in nanoseconds since epoch.
  // TODO(nellyv): Replace return value with a time/clock type.
  virtual int64_t GetTimestamp() const = 0;

  // Returns the contents of this commit.
  virtual std::unique_ptr<CommitContents> GetContents() const = 0;

  // Returns the id of the root node of this commit.
  virtual ObjectId GetRootId() const = 0;

  // Returns the byte representation of this |Commit| as they will be
  // synced to the cloud.
  virtual std::string GetStorageBytes() const = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Commit);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_PUBLIC_COMMIT_H_
