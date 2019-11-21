// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_

#include <set>

#include "src/ledger/bin/storage/impl/page_db.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

class PageDbBatchImpl : public PageDb::Batch {
 public:
  explicit PageDbBatchImpl(std::unique_ptr<Db::Batch> batch, PageDb* page_db,
                           ObjectIdentifierFactory* factory);
  PageDbBatchImpl(const PageDbBatchImpl&) = delete;
  PageDbBatchImpl& operator=(const PageDbBatchImpl&) = delete;
  ~PageDbBatchImpl() override;

  // Heads.
  Status AddHead(coroutine::CoroutineHandler* handler, CommitIdView head,
                 zx::time_utc timestamp) override;
  Status RemoveHead(coroutine::CoroutineHandler* handler, CommitIdView head) override;

  // Merges.
  Status AddMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                  CommitIdView parent2_id, CommitIdView merge_commit_id) override;
  Status DeleteMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                     CommitIdView parent2_id, CommitIdView commit_id) override;
  // Commits.
  Status AddCommitStorageBytes(coroutine::CoroutineHandler* handler, const CommitId& commit_id,
                               absl::string_view remote_commit_id,
                               const ObjectIdentifier& root_node,
                               absl::string_view storage_bytes) override;
  Status DeleteCommit(coroutine::CoroutineHandler* handler, CommitIdView commit_id,
                      absl::string_view remote_commit_id,
                      const ObjectIdentifier& root_node) override;

  // Object data.
  Status WriteObject(coroutine::CoroutineHandler* handler, const Piece& piece,
                     PageDbObjectStatus object_status,
                     const ObjectReferencesAndPriority& references) override;
  Status DeleteObject(coroutine::CoroutineHandler* handler, const ObjectDigest& object_digest,
                      const ObjectReferencesAndPriority& references) override;
  Status SetObjectStatus(coroutine::CoroutineHandler* handler,
                         const ObjectIdentifier& object_identifier,
                         PageDbObjectStatus object_status) override;

  // Commit sync metadata.
  Status MarkCommitIdSynced(coroutine::CoroutineHandler* handler,
                            const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(coroutine::CoroutineHandler* handler, const CommitId& commit_id,
                              uint64_t generation) override;

  // Object sync metadata.
  Status SetSyncMetadata(coroutine::CoroutineHandler* handler, absl::string_view key,
                         absl::string_view value) override;

  // Page online state.
  Status MarkPageOnline(coroutine::CoroutineHandler* handler) override;

  // Clocks
  Status SetDeviceId(coroutine::CoroutineHandler* handler,
                     const clocks::DeviceId& device_id) override;
  Status SetClock(coroutine::CoroutineHandler* handler, const Clock& clock) override;

  Status Execute(coroutine::CoroutineHandler* handler) override;

 private:
  // Stops tracking all deletions for this batch and clears |pending_deletions_|.
  // Returns false if any of the pending deletions was aborted by the object identifier factory
  // tracking them.
  bool UntrackPendingDeletions();

  Status DCheckHasObject(coroutine::CoroutineHandler* handler, const ObjectIdentifier& key);

  std::unique_ptr<Db::Batch> batch_;
  PageDb* const page_db_;
  ObjectIdentifierFactory* const factory_;
  // Object digests to be deleted when the batch is executed.
  std::set<ObjectDigest> pending_deletion_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_
