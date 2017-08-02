// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_db_empty_impl.h"

namespace storage {

Status PageDbEmptyImpl::Init() {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::CreateJournal(JournalType /*journal_type*/,
                                      const CommitId& /*base*/,
                                      std::unique_ptr<Journal>* /*journal*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::CreateMergeJournal(
    const CommitId& /*base*/,
    const CommitId& /*other*/,
    std::unique_ptr<Journal>* /*journal*/) {
  return Status::NOT_IMPLEMENTED;
}
std::unique_ptr<PageDb::Batch> PageDbEmptyImpl::StartBatch() {
  return nullptr;
}
Status PageDbEmptyImpl::GetHeads(std::vector<CommitId>* /*heads*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddHead(CommitIdView /*head*/, int64_t /*timestamp*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveHead(CommitIdView /*head*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetCommitStorageBytes(CommitIdView /*commit_id*/,
                                              std::string* /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddCommitStorageBytes(
    const CommitId& /*commit_id*/,
    ftl::StringView /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveCommit(const CommitId& /*commit_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetImplicitJournalIds(
    std::vector<JournalId>* /*journal_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetImplicitJournal(
    const JournalId& /*journal_id*/,
    std::unique_ptr<Journal>* /*journal*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveExplicitJournals() {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveJournal(const JournalId& /*journal_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddJournalEntry(const JournalId& /*journal_id*/,
                                        ftl::StringView /*key*/,
                                        ftl::StringView /*value*/,
                                        KeyPriority /*priority*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalValue(const JournalId& /*journal_id*/,
                                        ftl::StringView /*key*/,
                                        std::string* /*value*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveJournalEntry(
    const JournalId& /*journal_id*/,
    convert::ExtendedStringView /*key*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalEntries(
    const JournalId& /*journal_id*/,
    std::unique_ptr<Iterator<const EntryChange>>* /*entries*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::WriteObject(
    ObjectIdView /*object_id*/,
    std::unique_ptr<DataSource::DataChunk> /*content*/,
    ObjectStatus /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::ReadObject(ObjectId /*object_id*/,
                                   std::unique_ptr<const Object>* /*object*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::DeleteObject(ObjectIdView /*object_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalValueCounter(const JournalId& /*journal_id*/,
                                               ftl::StringView /*value*/,
                                               int64_t* /*counter*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetJournalValueCounter(const JournalId& /*journal_id*/,
                                               ftl::StringView /*value*/,
                                               int64_t /*counter*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalValues(const JournalId& /*journal_id*/,
                                         std::vector<std::string>* /*values*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetUnsyncedCommitIds(
    std::vector<CommitId>* /*commit_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::MarkCommitIdSynced(const CommitId& /*commit_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::MarkCommitIdUnsynced(const CommitId& /*commit_id*/,
                                             int64_t /*timestamp*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::IsCommitSynced(const CommitId& /*commit_id*/,
                                       bool* /*is_synced*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetUnsyncedPieces(
    std::vector<ObjectId>* /*object_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetObjectStatus(ObjectIdView /*object_id*/,
                                        ObjectStatus /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetObjectStatus(ObjectIdView /*object_id*/,
                                        ObjectStatus* /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetSyncMetadata(ftl::StringView /*key*/,
                                        ftl::StringView /*value*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetSyncMetadata(ftl::StringView /*key*/,
                                        std::string* /*value*/) {
  return Status::NOT_IMPLEMENTED;
}

}  // namespace storage
