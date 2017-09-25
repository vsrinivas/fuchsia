// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_

#include "lib/fxl/memory/ref_ptr.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace storage {

class CommitImpl : public Commit {
 public:
  ~CommitImpl() override;

  // Factory method for creating a |CommitImpl| object given its storage
  // representation. If the format is incorrect, |nullptr| will be returned.
  static std::unique_ptr<Commit> FromStorageBytes(PageStorage* page_storage,
                                                  CommitId id,
                                                  std::string storage_bytes);

  static std::unique_ptr<Commit> FromContentAndParents(
      PageStorage* page_storage,
      ObjectIdView root_node_id,
      std::vector<std::unique_ptr<const Commit>> parent_commits);

  // Factory method for creating an empty |CommitImpl| object, i.e. without
  // parents and with empty contents.
  static void Empty(
      PageStorage* page_storage,
      std::function<void(Status, std::unique_ptr<const Commit>)> callback);

  // Checks whether the given |storage_bytes| are a valid serialization of a
  // commit.
  static bool CheckValidSerialization(fxl::StringView storage_bytes);

  // Commit:
  std::unique_ptr<Commit> Clone() const override;
  const CommitId& GetId() const override;
  std::vector<CommitIdView> GetParentIds() const override;
  int64_t GetTimestamp() const override;
  uint64_t GetGeneration() const override;
  ObjectIdView GetRootId() const override;
  fxl::StringView GetStorageBytes() const override;

 private:
  class SharedStorageBytes;

  // Creates a new |CommitImpl| object with the given contents. |timestamp| is
  // the number of nanoseconds since epoch.
  CommitImpl(PageStorage* page_storage,
             CommitId id,
             int64_t timestamp,
             uint64_t generation,
             ObjectIdView root_node_id,
             std::vector<CommitIdView> parent_ids,
             fxl::RefPtr<SharedStorageBytes> storage_bytes);

  PageStorage* page_storage_;
  const CommitId id_;
  const int64_t timestamp_;
  const uint64_t generation_;
  const ObjectIdView root_node_id_;
  const std::vector<CommitIdView> parent_ids_;
  const fxl::RefPtr<SharedStorageBytes> storage_bytes_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_COMMIT_IMPL_H_
