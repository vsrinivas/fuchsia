// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/db_empty_impl.h"

namespace storage {

Status DbEmptyImpl::Init() {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::CreateJournal(JournalType journal_type,
                                  const CommitId& base,
                                  std::unique_ptr<Journal>* journal) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::CreateMergeJournal(const CommitId& base,
                                       const CommitId& other,
                                       std::unique_ptr<Journal>* journal) {
  return Status::NOT_IMPLEMENTED;
}

std::unique_ptr<DbEmptyImpl::Batch> DbEmptyImpl::StartBatch() {
  return nullptr;
}
Status DbEmptyImpl::GetHeads(std::vector<CommitId>* heads) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::AddHead(CommitIdView head, int64_t timestamp) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::RemoveHead(CommitIdView head) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetCommitStorageBytes(CommitIdView commit_id,
                                          std::string* storage_bytes) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::AddCommitStorageBytes(const CommitId& commit_id,
                                          ftl::StringView storage_bytes) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::RemoveCommit(const CommitId& commit_id) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetImplicitJournalIds(std::vector<JournalId>* journal_ids) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetImplicitJournal(const JournalId& journal_id,
                                       std::unique_ptr<Journal>* journal) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::RemoveExplicitJournals() {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::RemoveJournal(const JournalId& journal_id) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::AddJournalEntry(const JournalId& journal_id,
                                    ftl::StringView key,
                                    ftl::StringView value,
                                    KeyPriority priority) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetJournalValue(const JournalId& journal_id,
                                    ftl::StringView key,
                                    std::string* value) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::RemoveJournalEntry(const JournalId& journal_id,
                                       convert::ExtendedStringView key) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetJournalEntries(
    const JournalId& journal_id,
    std::unique_ptr<Iterator<const EntryChange>>* entries) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::WriteObject(ObjectIdView object_id,
                                std::unique_ptr<DataSource::DataChunk> content,
                                ObjectStatus object_status) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::ReadObject(ObjectId object_id,
                               std::unique_ptr<const Object>* object) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::DeleteObject(ObjectIdView object_id) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetJournalValueCounter(const JournalId& journal_id,
                                           ftl::StringView value,
                                           int64_t* counter) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::SetJournalValueCounter(const JournalId& journal_id,
                                           ftl::StringView value,
                                           int64_t counter) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetJournalValues(const JournalId& journal_id,
                                     std::vector<std::string>* values) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::MarkCommitIdSynced(const CommitId& commit_id) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::MarkCommitIdUnsynced(const CommitId& commit_id,
                                         int64_t timestamp) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::IsCommitSynced(const CommitId& commit_id, bool* is_synced) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetUnsyncedPieces(std::vector<ObjectId>* object_ids) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::SetObjectStatus(ObjectIdView object_id,
                                    ObjectStatus object_status) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetObjectStatus(ObjectIdView object_id,
                                    ObjectStatus* object_status) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::SetSyncMetadata(ftl::StringView key,
                                    ftl::StringView value) {
  return Status::NOT_IMPLEMENTED;
}
Status DbEmptyImpl::GetSyncMetadata(ftl::StringView key, std::string* value) {
  return Status::NOT_IMPLEMENTED;
}

}  // namespace storage
