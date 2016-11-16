// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_DB_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_DB_EMPTY_IMPL_H_

#include "apps/ledger/src/storage/impl/db.h"

namespace storage {

class DbEmptyImpl : public DB {
 public:
  DbEmptyImpl() {}
  ~DbEmptyImpl() {}

  Status Init() override;
  Status CreateJournal(JournalType journal_type,
                       const CommitId& base,
                       std::unique_ptr<Journal>* journal) override;
  Status CreateMergeJournal(const CommitId& base,
                            const CommitId& other,
                            std::unique_ptr<Journal>* journal) override;

  std::unique_ptr<Batch> StartBatch() override;
  Status GetHeads(std::vector<CommitId>* heads) override;
  Status AddHead(const CommitId& head) override;
  Status RemoveHead(const CommitId& head) override;
  Status ContainsHead(const CommitId& commit_id) override;
  Status GetCommitStorageBytes(const CommitId& commit_id,
                               std::string* storage_bytes) override;
  Status AddCommitStorageBytes(const CommitId& commit_id,
                               const std::string& storage_bytes) override;
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
  Status GetJournalValueCounter(const JournalId& journal_id,
                                ftl::StringView value,
                                int* counter) override;
  Status SetJournalValueCounter(const JournalId& journal_id,
                                ftl::StringView value,
                                int counter) override;
  Status GetJournalValues(const JournalId& journal_id,
                          std::vector<std::string>* values) override;
  Status GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) override;
  Status MarkCommitIdSynced(const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(const CommitId& commit_id) override;
  Status IsCommitSynced(const CommitId& commit_id, bool* is_synced) override;
  Status GetUnsyncedObjectIds(std::vector<ObjectId>* object_ids) override;
  Status MarkObjectIdSynced(ObjectIdView object_id) override;
  Status MarkObjectIdUnsynced(ObjectIdView object_id) override;
  Status IsObjectSynced(ObjectIdView object_id, bool* is_synced) override;
  Status SetNodeSize(size_t node_size) override;
  Status GetNodeSize(size_t* node_size) override;
  Status SetSyncMetadata(ftl::StringView sync_state) override;
  Status GetSyncMetadata(std::string* sync_state) override;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_DB_EMPTY_IMPL_H_
