// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/ledger/src/coroutine/coroutine.h"
#include "apps/ledger/src/storage/impl/leveldb.h"
#include "apps/ledger/src/storage/impl/page_db.h"
#include "lib/ftl/functional/auto_call.h"

namespace storage {

class PageStorageImpl;

// TODO(qsr): LE-250 There must be a mechanism to clean the database from
// TRANSIENT objects.
class PageDbImpl : public PageDb {
 public:
  PageDbImpl(coroutine::CoroutineService* coroutine_service,
             PageStorageImpl* page_storage,
             std::string db_path);
  ~PageDbImpl() override;

  Status Init() override;
  std::unique_ptr<PageDb::Batch> StartBatch() override;
  Status GetHeads(std::vector<CommitId>* heads) override;
  Status AddHead(CommitIdView head, int64_t timestamp) override;
  Status RemoveHead(CommitIdView head) override;
  Status GetCommitStorageBytes(CommitIdView commit_id,
                               std::string* storage_bytes) override;
  Status AddCommitStorageBytes(const CommitId& commit_id,
                               ftl::StringView storage_bytes) override;
  Status RemoveCommit(const CommitId& commit_id) override;
  Status CreateJournal(JournalType journal_type,
                       const CommitId& base,
                       std::unique_ptr<Journal>* journal) override;
  Status CreateMergeJournal(const CommitId& base,
                            const CommitId& other,
                            std::unique_ptr<Journal>* journal) override;
  Status GetImplicitJournalIds(std::vector<JournalId>* journal_ids) override;
  Status GetImplicitJournal(const JournalId& journal_id,
                            std::unique_ptr<Journal>* journal) override;
  Status RemoveExplicitJournals() override;
  Status RemoveJournal(const JournalId& journal_id) override;
  Status AddJournalEntry(const JournalId& journal_id,
                         ftl::StringView key,
                         ftl::StringView value,
                         KeyPriority priority) override;
  Status GetJournalValue(const JournalId& journal_id,
                         ftl::StringView key,
                         std::string* value) override;
  Status RemoveJournalEntry(const JournalId& journal_id,
                            convert::ExtendedStringView key) override;
  Status GetJournalEntries(
      const JournalId& journal_id,
      std::unique_ptr<Iterator<const EntryChange>>* entries) override;
  Status WriteObject(ObjectIdView object_id,
                     std::unique_ptr<DataSource::DataChunk> content,
                     ObjectStatus object_status) override;
  Status ReadObject(ObjectId object_id,
                    std::unique_ptr<const Object>* object) override;
  Status DeleteObject(ObjectIdView object_id) override;
  Status GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) override;
  Status MarkCommitIdSynced(const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(const CommitId& commit_id,
                              uint64_t generation) override;
  Status IsCommitSynced(const CommitId& commit_id, bool* is_synced) override;
  Status GetUnsyncedPieces(std::vector<ObjectId>* object_ids) override;
  Status SetObjectStatus(ObjectIdView object_id,
                         ObjectStatus object_status) override;
  Status GetObjectStatus(ObjectIdView object_id,
                         ObjectStatus* object_status) override;
  Status SetSyncMetadata(ftl::StringView key, ftl::StringView value) override;
  Status GetSyncMetadata(ftl::StringView key, std::string* value) override;

 private:
  std::unique_ptr<PageDb::Batch> StartLocalBatch();
  bool CheckHasKey(convert::ExtendedStringView key);

  coroutine::CoroutineService* const coroutine_service_;
  PageStorageImpl* const page_storage_;
  LevelDb db_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_IMPL_H_
