// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_
#define _APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_

#include "apps/ledger/src/coroutine/coroutine.h"
#include "apps/ledger/src/storage/impl/db.h"
#include "apps/ledger/src/storage/impl/page_db.h"

namespace storage {

class PageDbBatchImpl : public PageDb::Batch {
 public:
  explicit PageDbBatchImpl(std::unique_ptr<Db::Batch> batch,
                           PageDb* db,
                           coroutine::CoroutineService* coroutine_service,
                           PageStorageImpl* page_storage);
  ~PageDbBatchImpl() override;

  // Heads.
  Status AddHead(coroutine::CoroutineHandler* handler,
                 CommitIdView head,
                 int64_t timestamp) override;
  Status RemoveHead(coroutine::CoroutineHandler* handler,
                    CommitIdView head) override;

  // Commits.
  Status AddCommitStorageBytes(coroutine::CoroutineHandler* handler,
                               const CommitId& commit_id,
                               fxl::StringView storage_bytes) override;
  Status RemoveCommit(coroutine::CoroutineHandler* handler,
                      const CommitId& commit_id) override;

  // Journals.
  Status CreateJournal(coroutine::CoroutineHandler* handler,
                       JournalType journal_type,
                       const CommitId& base,
                       std::unique_ptr<Journal>* journal) override;
  Status CreateMergeJournal(coroutine::CoroutineHandler* handler,
                            const CommitId& base,
                            const CommitId& other,
                            std::unique_ptr<Journal>* journal) override;
  Status RemoveExplicitJournals(coroutine::CoroutineHandler* handler) override;
  Status RemoveJournal(const JournalId& journal_id) override;

  // Journal entries.
  Status AddJournalEntry(const JournalId& journal_id,
                         fxl::StringView key,
                         fxl::StringView value,
                         KeyPriority priority) override;
  Status RemoveJournalEntry(const JournalId& journal_id,
                            convert::ExtendedStringView key) override;

  // Object data.
  Status WriteObject(coroutine::CoroutineHandler* handler,
                     ObjectIdView object_id,
                     std::unique_ptr<DataSource::DataChunk> content,
                     PageDbObjectStatus object_status) override;
  Status DeleteObject(coroutine::CoroutineHandler* handler,
                      ObjectIdView object_id) override;
  Status SetObjectStatus(coroutine::CoroutineHandler* handler,
                         ObjectIdView object_id,
                         PageDbObjectStatus object_status) override;

  // Commit sync metadata.
  Status MarkCommitIdSynced(coroutine::CoroutineHandler* handler,
                            const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(coroutine::CoroutineHandler* handler,
                              const CommitId& commit_id,
                              uint64_t generation) override;

  // Object sync metadata.
  Status SetSyncMetadata(coroutine::CoroutineHandler* handler,
                         fxl::StringView key,
                         fxl::StringView value) override;

  Status Execute() override;

 private:
  bool CheckHasObject(convert::ExtendedStringView key);

  std::unique_ptr<Db::Batch> batch_;
  PageDb* db_;
  coroutine::CoroutineService* coroutine_service_;
  PageStorageImpl* page_storage_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDbBatchImpl);
};

}  // namespace storage

#endif  // _APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_BATCH_IMPL_H_
