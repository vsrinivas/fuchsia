// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_

#include "apps/ledger/storage/impl/store/object_store.h"
#include "apps/ledger/storage/public/commit.h"

namespace storage {

class CommitImpl : public Commit {
 public:
  ~CommitImpl() override;

  // Factory method for creating a |CommitImpl| object given its storage
  // representation. If the format is incorrect, a NULL pointer will be
  // returned.
  static std::unique_ptr<Commit> FromStorageBytes(ObjectStore* store,
                                                  const CommitId& id,
                                                  std::string&& storage_bytes);

  static std::unique_ptr<Commit> FromContentAndParents(
      ObjectStore* store,
      ObjectIdView root_node_id,
      std::vector<CommitId>&& parent_ids);

  // Factory method for creating an empty |CommitImpl| object, i.e. without
  // parents and with empty contents.
  static std::unique_ptr<Commit> Empty(ObjectStore* store);

  // Commit:
  CommitId GetId() const override;
  std::vector<CommitId> GetParentIds() const override;
  int64_t GetTimestamp() const override;
  std::unique_ptr<CommitContents> GetContents() const override;
  ObjectId GetRootId() const override;
  std::string GetStorageBytes() const override;

 private:
  // Creates a new |CommitImpl| object with the given contents. |timestamp| is
  // the number of nanoseconds since epoch.
  CommitImpl(ObjectStore* store,
             const CommitId& id,
             int64_t timestamp,
             ObjectIdView root_node_id,
             const std::vector<CommitId>& parent_ids,
             std::string&& storage_bytes);

  ObjectStore* store_;
  CommitId id_;
  int64_t timestamp_;
  ObjectId root_node_id_;
  std::vector<CommitId> parent_ids_;
  std::string storage_bytes_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_
