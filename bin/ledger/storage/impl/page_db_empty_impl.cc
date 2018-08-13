// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/page_db_empty_impl.h"

namespace storage {

using coroutine::CoroutineHandler;

Status PageDbEmptyImpl::Init(CoroutineHandler* /*handler*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::StartBatch(CoroutineHandler* /*handler*/,
                                   std::unique_ptr<PageDb::Batch>* /*batch*/) {
  return Status::NOT_IMPLEMENTED;
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
    CoroutineHandler* /*handler*/, std::vector<JournalId>* /*journal_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetBaseCommitForJournal(CoroutineHandler* /*handler*/,
                                                const JournalId& /*journal_id*/,
                                                CommitId* /*base*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetJournalEntries(
    CoroutineHandler* /*handler*/, const JournalId& /*journal_id*/,
    std::unique_ptr<Iterator<const EntryChange>>* /*entries*/,
    JournalContainsClearOperation* /*contains_clear_operation*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::ReadObject(CoroutineHandler* /*handler*/,
                                   ObjectIdentifier /*object_identifier*/,
                                   std::unique_ptr<const Object>* /*object*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::HasObject(CoroutineHandler* /*handler*/,
                                  ObjectDigestView /*object_digest*/,
                                  bool* /*has_object*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetObjectStatus(CoroutineHandler* /*handler*/,
                                        ObjectIdentifier /*object_identifier*/,
                                        PageDbObjectStatus* /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetUnsyncedCommitIds(
    CoroutineHandler* /*handler*/, std::vector<CommitId>* /*commit_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::IsCommitSynced(CoroutineHandler* /*handler*/,
                                       const CommitId& /*commit_id*/,
                                       bool* /*is_synced*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetUnsyncedPieces(
    CoroutineHandler* /*handler*/,
    std::vector<ObjectIdentifier>* /*object_identifiers*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetSyncMetadata(CoroutineHandler* /*handler*/,
                                        fxl::StringView /*key*/,
                                        std::string* /*value*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::IsPageOnline(coroutine::CoroutineHandler* /*handler*/,
                                     bool* /*page_is_online*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddHead(CoroutineHandler* /*handler*/,
                                CommitIdView /*head*/, int64_t /*timestamp*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveHead(CoroutineHandler* /*handler*/,
                                   CommitIdView /*head*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddCommitStorageBytes(
    CoroutineHandler* /*handler*/, const CommitId& /*commit_id*/,
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
Status PageDbEmptyImpl::AddJournalEntry(
    CoroutineHandler* /*handler*/, const JournalId& /*journal_id*/,
    fxl::StringView /*key*/, const ObjectIdentifier& /*object_identifier*/,
    KeyPriority /*priority*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveJournalEntry(
    CoroutineHandler* /*handler*/, const JournalId& /*journal_id*/,
    convert::ExtendedStringView /*key*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::EmptyJournalAndMarkContainsClearOperation(
    coroutine::CoroutineHandler* /*handler*/, const JournalId& /*journal_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::WriteObject(
    CoroutineHandler* /*handler*/, ObjectIdentifier /*object_identifier*/,
    std::unique_ptr<DataSource::DataChunk> /*content*/,
    PageDbObjectStatus /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetObjectStatus(CoroutineHandler* /*handler*/,
                                        ObjectIdentifier /*object_identifier*/,
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

Status PageDbEmptyImpl::MarkPageOnline(
    coroutine::CoroutineHandler* /*handlers*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::Execute(CoroutineHandler* /*handler*/) {
  return Status::NOT_IMPLEMENTED;
}

}  // namespace storage
