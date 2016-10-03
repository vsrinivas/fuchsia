// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_

#include "apps/ledger/storage/public/commit.h"

namespace storage {

class CommitImpl : public Commit {
 public:
  // Creates a new |CommitImpl| object with the given contents. |timestamp| is
  // the number of nanoseconds since epoch.
  CommitImpl(const CommitId& id,
             int64_t timestamp,
             const ObjectId& root_node_id,
             const std::vector<CommitId>& parent_ids);

  ~CommitImpl() override;

  // Factory method for creating a |CommitImpl| object given its storage
  // representation. If the format is incorrect, a NULL pointer will be
  // returned.
  static std::unique_ptr<Commit> FromStorageBytes(
      const CommitId& id,
      const std::string& storage_bytes);

  // Commit:
  CommitId GetId() const override;
  std::vector<CommitId> GetParentIds() const override;
  int64_t GetTimestamp() const override;
  std::unique_ptr<CommitContents> GetContents() const override;
  std::string GetStorageBytes() const override;

 private:
  CommitId id_;
  int64_t timestamp_;
  ObjectId root_node_id_;
  std::vector<CommitId> parent_ids_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_
