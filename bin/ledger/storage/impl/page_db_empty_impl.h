// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_

#include "apps/ledger/src/storage/impl/page_db.h"

namespace storage {

class PageDbEmptyImpl : public PageDb, public PageDb::Batch {
 public:
  PageDbEmptyImpl() {}
  ~PageDbEmptyImpl() override {}

  // PageDb:
  Status Init() override;
  std::unique_ptr<PageDb::Batch> StartBatch() override;
  Status GetHeads(coroutine::CoroutineHandler* handler,
                  std::vector<CommitId>* heads) override;
  Status GetCommitStorageBytes(coroutine::CoroutineHandler* handler,
                               CommitIdView commit_id,
                               std::string* storage_bytes) override;
  Status GetImplicitJournalIds(coroutine::CoroutineHandler* handler,
                               std::vector<JournalId>* journal_ids) override;
  Status GetBaseCommitForJournal(coroutine::CoroutineHandler* handler,
                                 const JournalId& journal_id,
                                 CommitId* base) override;
  Status GetJournalEntries(
      coroutine::CoroutineHandler* handler,
      const JournalId& journal_id,
      std::unique_ptr<Iterator<const EntryChange>>* entries) override;

  // PageDb and PageDb::Batch:
  Status ReadObject(coroutine::CoroutineHandler* handler,
                    ObjectId object_id,
                    std::unique_ptr<const Object>* object) override;
  Status HasObject(coroutine::CoroutineHandler* handler,
                   ObjectIdView object_id,
                   bool* has_object) override;
  Status GetUnsyncedCommitIds(coroutine::CoroutineHandler* handler,
                              std::vector<CommitId>* commit_ids) override;
  Status IsCommitSynced(coroutine::CoroutineHandler* handler,
                        const CommitId& commit_id,
                        bool* is_synced) override;
  Status GetUnsyncedPieces(coroutine::CoroutineHandler* handler,
                           std::vector<ObjectId>* object_ids) override;
  Status GetObjectStatus(coroutine::CoroutineHandler* handler,
                         ObjectIdView object_id,
                         PageDbObjectStatus* object_status) override;
  Status GetSyncMetadata(fxl::StringView key, std::string* value) override;

  Status AddHead(coroutine::CoroutineHandler* handler,
                 CommitIdView head,
                 int64_t timestamp) override;
  Status RemoveHead(coroutine::CoroutineHandler* handler,
                    CommitIdView head) override;
  Status AddCommitStorageBytes(coroutine::CoroutineHandler* handler,
                               const CommitId& commit_id,
                               fxl::StringView storage_bytes) override;
  Status RemoveCommit(coroutine::CoroutineHandler* handler,
                      const CommitId& commit_id) override;
  Status CreateJournalId(coroutine::CoroutineHandler* handler,
                         JournalType journal_type,
                         const CommitId& base,
                         JournalId* journal) override;
  Status RemoveExplicitJournals(coroutine::CoroutineHandler* handler) override;
  Status RemoveJournal(coroutine::CoroutineHandler* handler,
                       const JournalId& journal_id) override;
  Status AddJournalEntry(coroutine::CoroutineHandler* handler,
                         const JournalId& journal_id,
                         fxl::StringView key,
                         fxl::StringView value,
                         KeyPriority priority) override;
  Status RemoveJournalEntry(coroutine::CoroutineHandler* handler,
                            const JournalId& journal_id,
                            convert::ExtendedStringView key) override;
  Status WriteObject(coroutine::CoroutineHandler* handler,
                     ObjectIdView object_id,
                     std::unique_ptr<DataSource::DataChunk> content,
                     PageDbObjectStatus object_status) override;
  Status DeleteObject(coroutine::CoroutineHandler* handler,
                      ObjectIdView object_id) override;
  Status MarkCommitIdSynced(coroutine::CoroutineHandler* handler,
                            const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(coroutine::CoroutineHandler* handler,
                              const CommitId& commit_id,
                              uint64_t generation) override;
  Status SetObjectStatus(coroutine::CoroutineHandler* handler,
                         ObjectIdView object_id,
                         PageDbObjectStatus object_status) override;
  Status SetSyncMetadata(coroutine::CoroutineHandler* handler,
                         fxl::StringView key,
                         fxl::StringView value) override;

  // PageDb::Batch:
  Status Execute() override;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_
