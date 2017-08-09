// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_

#include "apps/ledger/src/storage/impl/page_db.h"

namespace storage {

class PageDbEmptyImpl : public PageDb {
 public:
  PageDbEmptyImpl() {}
  ~PageDbEmptyImpl() override {}

  Status Init() override;
  Status CreateJournal(JournalType journal_type,
                       const CommitId& base,
                       std::unique_ptr<Journal>* journal) override;
  Status CreateMergeJournal(const CommitId& base,
                            const CommitId& other,
                            std::unique_ptr<Journal>* journal) override;

  std::unique_ptr<PageDb::Batch> StartBatch() override;
  Status GetHeads(std::vector<CommitId>* heads) override;
  Status AddHead(CommitIdView head, int64_t timestamp) override;
  Status RemoveHead(CommitIdView head) override;
  Status GetCommitStorageBytes(CommitIdView commit_id,
                               std::string* storage_bytes) override;
  Status AddCommitStorageBytes(const CommitId& commit_id,
                               ftl::StringView storage_bytes) override;
  Status RemoveCommit(const CommitId& commit_id) override;
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
  Status GetJournalValueCounter(const JournalId& journal_id,
                                ftl::StringView value,
                                int64_t* counter) override;
  Status SetJournalValueCounter(const JournalId& journal_id,
                                ftl::StringView value,
                                int64_t counter) override;
  Status GetJournalValues(const JournalId& journal_id,
                          std::vector<std::string>* values) override;
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
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_PAGE_DB_EMPTY_IMPL_H_
