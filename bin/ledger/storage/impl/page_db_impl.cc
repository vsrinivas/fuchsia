// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/page_db_impl.h"

#include <algorithm>
#include <string>

#include <lib/fxl/strings/concatenate.h>

#include "peridot/bin/ledger/storage/impl/db_serialization.h"
#include "peridot/bin/ledger/storage/impl/journal_impl.h"
#include "peridot/bin/ledger/storage/impl/number_serialization.h"
#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"
#include "peridot/bin/ledger/storage/impl/object_impl.h"
#include "peridot/bin/ledger/storage/impl/page_db_batch_impl.h"
#include "peridot/lib/convert/convert.h"

#define RETURN_ON_ERROR(expr)   \
  do {                          \
    Status status = (expr);     \
    if (status != Status::OK) { \
      return status;            \
    }                           \
  } while (0)

namespace storage {

using coroutine::CoroutineHandler;

namespace {

void ExtractSortedCommitsIds(
    std::vector<std::pair<std::string, std::string>>* entries,
    std::vector<CommitId>* commit_ids) {
  std::sort(entries->begin(), entries->end(),
            [](const std::pair<std::string, std::string>& p1,
               const std::pair<std::string, std::string>& p2) {
              auto t1 = DeserializeNumber<int64_t>(p1.second);
              auto t2 = DeserializeNumber<int64_t>(p2.second);
              if (t1 != t2) {
                return t1 < t2;
              }
              return p1.first < p2.first;
            });
  commit_ids->clear();
  commit_ids->reserve(entries->size());
  for (std::pair<std::string, std::string>& entry : *entries) {
    commit_ids->push_back(std::move(entry.first));
  }
}

class JournalEntryIterator final : public Iterator<const EntryChange> {
 public:
  explicit JournalEntryIterator(
      std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                               convert::ExtendedStringView>>>
          it)
      : it_(std::move(it)) {
    PrepareEntry();
  }

  ~JournalEntryIterator() override {}

  Iterator<const EntryChange>& Next() override {
    it_->Next();
    PrepareEntry();
    return *this;
  }

  bool Valid() const override { return it_->Valid(); }

  Status GetStatus() const override { return it_->GetStatus(); }

  const EntryChange& operator*() const override { return *(change_.get()); }
  const EntryChange* operator->() const override { return change_.get(); }

 private:
  void PrepareEntry() {
    if (!JournalEntryIterator::Valid()) {
      change_.reset(nullptr);
      return;
    }
    change_ = std::make_unique<EntryChange>();

    const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>&
        key_value = **it_;
    change_->entry.key =
        key_value.first.substr(JournalEntryRow::kPrefixSize).ToString();

    if (key_value.second[0] == JournalEntryRow::kAddPrefix) {
      Status status = JournalEntryRow::ExtractObjectIdentifier(
          key_value.second, &change_->entry.object_identifier);
      FXL_DCHECK(status == Status::OK);
      change_->deleted = false;
      change_->entry.priority =
          (key_value.second[1] == JournalEntryRow::kLazyPrefix)
              ? KeyPriority::LAZY
              : KeyPriority::EAGER;
    } else {
      change_->deleted = true;
    }
  }

  std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                           convert::ExtendedStringView>>>
      it_;

  std::unique_ptr<EntryChange> change_;
};

}  // namespace

PageDbImpl::PageDbImpl(async_dispatcher_t* dispatcher,
                       ledger::DetachedPath db_path)
    : db_(dispatcher, std::move(db_path)) {}

PageDbImpl::~PageDbImpl() {}

Status PageDbImpl::Init(coroutine::CoroutineHandler* handler) { return db_.Init(); }

Status PageDbImpl::StartBatch(coroutine::CoroutineHandler* handler,
                              std::unique_ptr<Batch>* batch) {
  std::unique_ptr<Db::Batch> db_batch;
  RETURN_ON_ERROR(db_.StartBatch(handler, &db_batch));
  *batch = std::make_unique<PageDbBatchImpl>(std::move(db_batch), this);
  return Status::OK;
}

Status PageDbImpl::GetHeads(CoroutineHandler* handler,
                            std::vector<CommitId>* heads) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(db_.GetEntriesByPrefix(
      handler, convert::ToSlice(HeadRow::kPrefix), &entries));
  ExtractSortedCommitsIds(&entries, heads);
  return Status::OK;
}

Status PageDbImpl::GetCommitStorageBytes(CoroutineHandler* handler,
                                         CommitIdView commit_id,
                                         std::string* storage_bytes) {
  return db_.Get(handler, CommitRow::GetKeyFor(commit_id), storage_bytes);
}

Status PageDbImpl::GetImplicitJournalIds(CoroutineHandler* handler,
                                         std::vector<JournalId>* journal_ids) {
  return db_.GetByPrefix(handler,
                         convert::ToSlice(ImplicitJournalMetadataRow::kPrefix),
                         journal_ids);
}

Status PageDbImpl::GetBaseCommitForJournal(CoroutineHandler* handler,
                                           const JournalId& journal_id,
                                           CommitId* base) {
  FXL_DCHECK(journal_id.size() == JournalEntryRow::kJournalIdSize);
  FXL_DCHECK(journal_id[0] == JournalEntryRow::kImplicitPrefix);
  return db_.Get(handler, ImplicitJournalMetadataRow::GetKeyFor(journal_id),
                 base);
}

Status PageDbImpl::GetJournalEntries(
    CoroutineHandler* handler, const JournalId& journal_id,
    std::unique_ptr<Iterator<const EntryChange>>* entries) {
  std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                           convert::ExtendedStringView>>>
      it;
  RETURN_ON_ERROR(db_.GetIteratorAtPrefix(
      handler, JournalEntryRow::GetPrefixFor(journal_id), &it));

  *entries = std::make_unique<JournalEntryIterator>(std::move(it));
  return Status::OK;
}

Status PageDbImpl::ReadObject(CoroutineHandler* handler,
                              ObjectIdentifier object_identifier,
                              std::unique_ptr<const Object>* object) {
  return db_.GetObject(handler,
                       ObjectRow::GetKeyFor(object_identifier.object_digest),
                       object_identifier, object);
}

Status PageDbImpl::HasObject(CoroutineHandler* handler,
                             ObjectDigestView object_digest, bool* has_object) {
  return db_.HasKey(handler, ObjectRow::GetKeyFor(object_digest), has_object);
}

Status PageDbImpl::GetObjectStatus(CoroutineHandler* handler,
                                   ObjectIdentifier object_identifier,
                                   PageDbObjectStatus* object_status) {
  // Check must be done in ascending order of status, so that a change of status
  // between 2 reads does not create the case where no key is found.
  // That said, the most common expected status is SYNCED, so for performance
  // reasons, it is better to check it first.
  // By checking it first and then checking all statuses in ascending order we
  // both ensure correctness and performant lookup.
  // The only case that would generate a spurious lookup is when the status is
  // changed concurrently, which is a rare occurence.
  for (PageDbObjectStatus possible_status :
       {PageDbObjectStatus::SYNCED, PageDbObjectStatus::TRANSIENT,
        PageDbObjectStatus::LOCAL, PageDbObjectStatus::SYNCED}) {
    bool has_key;
    RETURN_ON_ERROR(db_.HasKey(
        handler, ObjectStatusRow::GetKeyFor(possible_status, object_identifier),
        &has_key));
    if (has_key) {
      *object_status = possible_status;
      return Status::OK;
    }
  }

  *object_status = PageDbObjectStatus::UNKNOWN;
  return Status::OK;
}

Status PageDbImpl::GetUnsyncedCommitIds(CoroutineHandler* handler,
                                        std::vector<CommitId>* commit_ids) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(db_.GetEntriesByPrefix(
      handler, convert::ToSlice(UnsyncedCommitRow::kPrefix), &entries));
  ExtractSortedCommitsIds(&entries, commit_ids);
  return Status::OK;
}

Status PageDbImpl::IsCommitSynced(CoroutineHandler* handler,
                                  const CommitId& commit_id, bool* is_synced) {
  bool has_key;
  RETURN_ON_ERROR(
      db_.HasKey(handler, UnsyncedCommitRow::GetKeyFor(commit_id), &has_key));
  *is_synced = !has_key;
  return Status::OK;
}

Status PageDbImpl::GetUnsyncedPieces(
    CoroutineHandler* handler,
    std::vector<ObjectIdentifier>* object_identifiers) {
  std::vector<std::string> encoded_identifiers;
  Status status =
      db_.GetByPrefix(handler, convert::ToSlice(ObjectStatusRow::kLocalPrefix),
                      &encoded_identifiers);
  if (status != Status::OK) {
    return status;
  }

  object_identifiers->clear();
  ObjectIdentifier object_identifier;
  for (auto& encoded_identifier : encoded_identifiers) {
    if (!DecodeObjectIdentifier(encoded_identifier, &object_identifier)) {
      return Status::FORMAT_ERROR;
    }
    object_identifiers->emplace_back(std::move(object_identifier));
  }

  return Status::OK;
}

Status PageDbImpl::GetSyncMetadata(CoroutineHandler* handler,
                                   fxl::StringView key, std::string* value) {
  return db_.Get(handler, SyncMetadataRow::GetKeyFor(key), value);
}

Status PageDbImpl::IsPageOnline(coroutine::CoroutineHandler* handler,
                                bool* page_is_online) {
  return db_.HasKey(handler, PageIsOnlineRow::kKey, page_is_online);
}

Status PageDbImpl::AddHead(CoroutineHandler* handler, CommitIdView head,
                           int64_t timestamp) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->AddHead(handler, head, timestamp));
  return batch->Execute(handler);
}

Status PageDbImpl::RemoveHead(CoroutineHandler* handler, CommitIdView head) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->RemoveHead(handler, head));
  return batch->Execute(handler);
}

Status PageDbImpl::AddCommitStorageBytes(CoroutineHandler* handler,
                                         const CommitId& commit_id,
                                         fxl::StringView storage_bytes) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(
      batch->AddCommitStorageBytes(handler, commit_id, storage_bytes));
  return batch->Execute(handler);
}

Status PageDbImpl::RemoveCommit(CoroutineHandler* handler,
                                const CommitId& commit_id) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->RemoveCommit(handler, commit_id));
  return batch->Execute(handler);
}

Status PageDbImpl::CreateJournalId(CoroutineHandler* handler,
                                   JournalType journal_type,
                                   const CommitId& base,
                                   JournalId* journal_id) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(
      batch->CreateJournalId(handler, journal_type, base, journal_id));
  return batch->Execute(handler);
}

Status PageDbImpl::RemoveExplicitJournals(CoroutineHandler* handler) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->RemoveExplicitJournals(handler));
  return batch->Execute(handler);
}

Status PageDbImpl::RemoveJournal(CoroutineHandler* handler,
                                 const JournalId& journal_id) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->RemoveJournal(handler, journal_id));
  return batch->Execute(handler);
}

Status PageDbImpl::AddJournalEntry(CoroutineHandler* handler,
                                   const JournalId& journal_id,
                                   fxl::StringView key,
                                   const ObjectIdentifier& object_identifier,
                                   KeyPriority priority) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->AddJournalEntry(handler, journal_id, key,
                                         object_identifier, priority));
  return batch->Execute(handler);
}

Status PageDbImpl::RemoveJournalEntry(CoroutineHandler* handler,
                                      const JournalId& journal_id,
                                      convert::ExtendedStringView key) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->RemoveJournalEntry(handler, journal_id, key));
  return batch->Execute(handler);
}

Status PageDbImpl::WriteObject(CoroutineHandler* handler,
                               ObjectIdentifier object_identifier,
                               std::unique_ptr<DataSource::DataChunk> content,
                               PageDbObjectStatus object_status) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->WriteObject(handler, object_identifier,
                                     std::move(content), object_status));
  return batch->Execute(handler);
}

Status PageDbImpl::SetObjectStatus(CoroutineHandler* handler,
                                   ObjectIdentifier object_identifier,
                                   PageDbObjectStatus object_status) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(
      batch->SetObjectStatus(handler, object_identifier, object_status));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkCommitIdSynced(CoroutineHandler* handler,
                                      const CommitId& commit_id) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkCommitIdSynced(handler, commit_id));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkCommitIdUnsynced(CoroutineHandler* handler,
                                        const CommitId& commit_id,
                                        uint64_t generation) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkCommitIdUnsynced(handler, commit_id, generation));
  return batch->Execute(handler);
}

Status PageDbImpl::SetSyncMetadata(CoroutineHandler* handler,
                                   fxl::StringView key, fxl::StringView value) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->SetSyncMetadata(handler, key, value));
  return batch->Execute(handler);
}

Status PageDbImpl::MarkPageOnline(coroutine::CoroutineHandler* handler) {
  std::unique_ptr<Batch> batch;
  RETURN_ON_ERROR(StartBatch(handler, &batch));
  RETURN_ON_ERROR(batch->MarkPageOnline(handler));
  return batch->Execute(handler);
}

}  // namespace storage
