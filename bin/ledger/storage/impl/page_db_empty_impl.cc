// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_db_empty_impl.h"

namespace storage {

Status PageDbEmptyImpl::Init() {
  return Status::NOT_IMPLEMENTED;
}
std::unique_ptr<PageDb::Batch> PageDbEmptyImpl::StartBatch() {
  return std::make_unique<PageDbEmptyImpl>();
}
Status PageDbEmptyImpl::GetHeads(coroutine::CoroutineHandler* /*handler*/,
                                 std::vector<CommitId>* /*heads*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetCommitStorageBytes(
    coroutine::CoroutineHandler* /*handler*/,
    CommitIdView /*commit_id*/,
    std::string* /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetImplicitJournalIds(
    coroutine::CoroutineHandler* /*handler*/,
    std::vector<JournalId>* /*journal_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetImplicitJournal(
    coroutine::CoroutineHandler* /*handler*/,
    const JournalId& /*journal_id*/,
    std::unique_ptr<Journal>* /*journal*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalValue(const JournalId& /*journal_id*/,
                                        ftl::StringView /*key*/,
                                        std::string* /*value*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalEntries(
    const JournalId& /*journal_id*/,
    std::unique_ptr<Iterator<const EntryChange>>* /*entries*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::ReadObject(ObjectId /*object_id*/,
                                   std::unique_ptr<const Object>* /*object*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::HasObject(ObjectIdView /*object_id*/,
                                  bool* /*has_object*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetObjectStatus(ObjectIdView /*object_id*/,
                                        PageDbObjectStatus* /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetUnsyncedCommitIds(
    std::vector<CommitId>* /*commit_ids*/) {
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
Status PageDbEmptyImpl::GetSyncMetadata(ftl::StringView /*key*/,
                                        std::string* /*value*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddHead(coroutine::CoroutineHandler* /*handler*/,
                                CommitIdView /*head*/,
                                int64_t /*timestamp*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveHead(coroutine::CoroutineHandler* /*handler*/,
                                   CommitIdView /*head*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddCommitStorageBytes(
    coroutine::CoroutineHandler* /*handler*/,
    const CommitId& /*commit_id*/,
    ftl::StringView /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveCommit(coroutine::CoroutineHandler* /*handler*/,
                                     const CommitId& /*commit_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::CreateJournal(coroutine::CoroutineHandler* /*handler*/,
                                      JournalType /*journal_type*/,
                                      const CommitId& /*base*/,
                                      std::unique_ptr<Journal>* /*journal*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::CreateMergeJournal(
    coroutine::CoroutineHandler* /*handler*/,
    const CommitId& /*base*/,
    const CommitId& /*other*/,
    std::unique_ptr<Journal>* /*journal*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveExplicitJournals(
    coroutine::CoroutineHandler* /*handler*/) {
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
Status PageDbEmptyImpl::RemoveJournalEntry(
    const JournalId& /*journal_id*/,
    convert::ExtendedStringView /*key*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::WriteObject(
    coroutine::CoroutineHandler* /*handler*/,
    ObjectIdView /*object_id*/,
    std::unique_ptr<DataSource::DataChunk> /*content*/,
    PageDbObjectStatus /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::DeleteObject(coroutine::CoroutineHandler* /*handler*/,
                                     ObjectIdView /*object_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetObjectStatus(
    coroutine::CoroutineHandler* /*handler*/,
    ObjectIdView /*object_id*/,
    PageDbObjectStatus /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::MarkCommitIdSynced(const CommitId& /*commit_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::MarkCommitIdUnsynced(const CommitId& /*commit_id*/,
                                             uint64_t /*generation*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetSyncMetadata(
    coroutine::CoroutineHandler* /*handler*/,
    ftl::StringView /*key*/,
    ftl::StringView /*value*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::Execute() {
  return Status::NOT_IMPLEMENTED;
}

}  // namespace storage
