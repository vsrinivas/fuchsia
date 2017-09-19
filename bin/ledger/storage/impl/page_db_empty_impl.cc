// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_db_empty_impl.h"

namespace storage {

using coroutine::CoroutineHandler;

Status PageDbEmptyImpl::Init() {
  return Status::NOT_IMPLEMENTED;
}
std::unique_ptr<PageDb::Batch> PageDbEmptyImpl::StartBatch() {
  return std::make_unique<PageDbEmptyImpl>();
}
Status PageDbEmptyImpl::GetHeads(CoroutineHandler* /*handler*/,
                                 std::vector<CommitId>* /*heads*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetCommitStorageBytes(CoroutineHandler* /*handler*/,
                                              CommitIdView /*commit_id*/,
                                              std::string* /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetImplicitJournalIds(
    CoroutineHandler* /*handler*/,
    std::vector<JournalId>* /*journal_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetBaseCommitForJournal(CoroutineHandler* /*handler*/,
                                                const JournalId& /*journal_id*/,
                                                CommitId* /*base*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalEntries(
    CoroutineHandler* /*handler*/,
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
    CoroutineHandler* /*handler*/,
    std::vector<CommitId>* /*commit_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::IsCommitSynced(CoroutineHandler* /*handler*/,
                                       const CommitId& /*commit_id*/,
                                       bool* /*is_synced*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetUnsyncedPieces(
    std::vector<ObjectId>* /*object_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetSyncMetadata(fxl::StringView /*key*/,
                                        std::string* /*value*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddHead(CoroutineHandler* /*handler*/,
                                CommitIdView /*head*/,
                                int64_t /*timestamp*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveHead(CoroutineHandler* /*handler*/,
                                   CommitIdView /*head*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddCommitStorageBytes(
    CoroutineHandler* /*handler*/,
    const CommitId& /*commit_id*/,
    fxl::StringView /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveCommit(CoroutineHandler* /*handler*/,
                                     const CommitId& /*commit_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::CreateJournalId(CoroutineHandler* /*handler*/,
                                        JournalType /*journal_type*/,
                                        const CommitId& /*base*/,
                                        JournalId* /*journal*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveExplicitJournals(CoroutineHandler* /*handler*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveJournal(CoroutineHandler* /*handler*/,
                                      const JournalId& /*journal_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddJournalEntry(CoroutineHandler* /*handler*/,
                                        const JournalId& /*journal_id*/,
                                        fxl::StringView /*key*/,
                                        fxl::StringView /*value*/,
                                        KeyPriority /*priority*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveJournalEntry(
    CoroutineHandler* /*handler*/,
    const JournalId& /*journal_id*/,
    convert::ExtendedStringView /*key*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::WriteObject(
    CoroutineHandler* /*handler*/,
    ObjectIdView /*object_id*/,
    std::unique_ptr<DataSource::DataChunk> /*content*/,
    PageDbObjectStatus /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::DeleteObject(CoroutineHandler* /*handler*/,
                                     ObjectIdView /*object_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetObjectStatus(CoroutineHandler* /*handler*/,
                                        ObjectIdView /*object_id*/,
                                        PageDbObjectStatus /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::MarkCommitIdSynced(CoroutineHandler* /*handler*/,
                                           const CommitId& /*commit_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::MarkCommitIdUnsynced(CoroutineHandler* /*handler*/,
                                             const CommitId& /*commit_id*/,
                                             uint64_t /*generation*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetSyncMetadata(CoroutineHandler* /*handler*/,
                                        fxl::StringView /*key*/,
                                        fxl::StringView /*value*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::Execute() {
  return Status::NOT_IMPLEMENTED;
}

}  // namespace storage
