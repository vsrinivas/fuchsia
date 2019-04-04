// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_

#include "src/ledger/bin/storage/impl/page_db.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace storage {

class PageDbBatchImpl : public PageDb::Batch {
 public:
  explicit PageDbBatchImpl(std::unique_ptr<Db::Batch> batch, PageDb* db);
  ~PageDbBatchImpl() override;

  // Heads.
  Status AddHead(coroutine::CoroutineHandler* handler, CommitIdView head,
                 zx::time_utc timestamp) override;
  Status RemoveHead(coroutine::CoroutineHandler* handler,
                    CommitIdView head) override;

  // Merges.
  Status AddMerge(coroutine::CoroutineHandler* handler, CommitIdView parent1_id,
                  CommitIdView parent2_id,
                  CommitIdView merge_commit_id) override;
  // Commits.
  Status AddCommitStorageBytes(coroutine::CoroutineHandler* handler,
                               const CommitId& commit_id,
                               const ObjectIdentifier& root_node,
                               fxl::StringView storage_bytes) override;

  // Object data.
  Status WriteObject(coroutine::CoroutineHandler* handler, const Piece& piece,
                     PageDbObjectStatus object_status,
                     const ObjectReferencesAndPriority& references) override;
  Status SetObjectStatus(coroutine::CoroutineHandler* handler,
                         const ObjectIdentifier& object_identifier,
                         PageDbObjectStatus object_status) override;

  // Commit sync metadata.
  Status MarkCommitIdSynced(coroutine::CoroutineHandler* handler,
                            const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(coroutine::CoroutineHandler* handler,
                              const CommitId& commit_id,
                              uint64_t generation) override;

  // Object sync metadata.
  Status SetSyncMetadata(coroutine::CoroutineHandler* handler,
                         fxl::StringView key, fxl::StringView value) override;

  // Page online state.
  Status MarkPageOnline(coroutine::CoroutineHandler* handler) override;

  Status Execute(coroutine::CoroutineHandler* handler) override;

 private:
  Status DCheckHasObject(coroutine::CoroutineHandler* handler,
                         const ObjectIdentifier& key);

  std::unique_ptr<Db::Batch> batch_;
  PageDb* const db_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDbBatchImpl);
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_
