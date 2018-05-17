// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_PAGE_DB_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_PAGE_DB_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <lib/async/dispatcher.h>

#include "lib/fxl/functional/auto_call.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/impl/leveldb.h"
#include "peridot/bin/ledger/storage/impl/page_db.h"

namespace storage {

class PageStorageImpl;

// TODO(qsr): LE-250 There must be a mechanism to clean the database from
// TRANSIENT objects.
class PageDbImpl : public PageDb {
 public:
  PageDbImpl(async_t* async, ledger::DetachedPath db_path);
  ~PageDbImpl() override;

  Status Init() override;
  Status StartBatch(coroutine::CoroutineHandler* handler,
                    std::unique_ptr<PageDb::Batch>* batch) override;
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
      coroutine::CoroutineHandler* handler, const JournalId& journal_id,
      std::unique_ptr<Iterator<const EntryChange>>* entries) override;
  Status ReadObject(coroutine::CoroutineHandler* handler,
                    ObjectIdentifier object_identifier,
                    std::unique_ptr<const Object>* object) override;
  Status HasObject(coroutine::CoroutineHandler* handler,
                   ObjectDigestView object_digest, bool* has_object) override;
  Status GetUnsyncedCommitIds(coroutine::CoroutineHandler* handler,
                              std::vector<CommitId>* commit_ids) override;
  Status IsCommitSynced(coroutine::CoroutineHandler* handler,
                        const CommitId& commit_id, bool* is_synced) override;
  Status GetUnsyncedPieces(
      coroutine::CoroutineHandler* handler,
      std::vector<ObjectIdentifier>* object_identifiers) override;
  Status GetObjectStatus(coroutine::CoroutineHandler* handler,
                         ObjectIdentifier object_identifier,
                         PageDbObjectStatus* object_status) override;
  Status GetSyncMetadata(coroutine::CoroutineHandler* handler,
                         fxl::StringView key, std::string* value) override;

  Status AddHead(coroutine::CoroutineHandler* handler, CommitIdView head,
                 int64_t timestamp) override;
  Status RemoveHead(coroutine::CoroutineHandler* handler,
                    CommitIdView head) override;
  Status AddCommitStorageBytes(coroutine::CoroutineHandler* handler,
                               const CommitId& commit_id,
                               fxl::StringView storage_bytes) override;
  Status RemoveCommit(coroutine::CoroutineHandler* handler,
                      const CommitId& commit_id) override;
  Status CreateJournalId(coroutine::CoroutineHandler* handler,
                         JournalType journal_type, const CommitId& base,
                         JournalId* journal_id) override;
  Status RemoveExplicitJournals(coroutine::CoroutineHandler* handler) override;
  Status RemoveJournal(coroutine::CoroutineHandler* handler,
                       const JournalId& journal_id) override;
  Status AddJournalEntry(coroutine::CoroutineHandler* handler,
                         const JournalId& journal_id, fxl::StringView key,
                         const ObjectIdentifier& object_identifier,
                         KeyPriority priority) override;
  Status RemoveJournalEntry(coroutine::CoroutineHandler* handler,
                            const JournalId& journal_id,
                            convert::ExtendedStringView key) override;
  Status WriteObject(coroutine::CoroutineHandler* handler,
                     ObjectIdentifier object_identifier,
                     std::unique_ptr<DataSource::DataChunk> content,
                     PageDbObjectStatus object_status) override;
  Status MarkCommitIdSynced(coroutine::CoroutineHandler* handler,
                            const CommitId& commit_id) override;
  Status MarkCommitIdUnsynced(coroutine::CoroutineHandler* handler,
                              const CommitId& commit_id,
                              uint64_t generation) override;
  Status SetObjectStatus(coroutine::CoroutineHandler* handler,
                         ObjectIdentifier object_identifier,
                         PageDbObjectStatus object_status) override;
  Status SetSyncMetadata(coroutine::CoroutineHandler* handler,
                         fxl::StringView key, fxl::StringView value) override;

 private:
  LevelDb db_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_PAGE_DB_IMPL_H_
