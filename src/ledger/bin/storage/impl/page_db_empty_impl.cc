// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_db_empty_impl.h"

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

using coroutine::CoroutineHandler;

Status PageDbEmptyImpl::StartBatch(CoroutineHandler* /*handler*/,
                                   std::unique_ptr<PageDb::Batch>* /*batch*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetHeads(CoroutineHandler* /*handler*/,
                                 std::vector<std::pair<zx::time_utc, CommitId>>* /*heads*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetMerges(coroutine::CoroutineHandler* /*handler*/,
                                  CommitIdView /*commit1_id*/, CommitIdView /*commit2_id*/,
                                  std::vector<CommitId>* /*merges*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetCommitStorageBytes(CoroutineHandler* /*handler*/,
                                              CommitIdView /*commit_id*/,
                                              std::string* /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::ReadObject(CoroutineHandler* /*handler*/,
                                   const ObjectIdentifier& /*object_identifier*/,
                                   std::unique_ptr<const Piece>* /*piece*/
) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::HasObject(CoroutineHandler* /*handler*/,
                                  const ObjectIdentifier& /*object_identifier*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetObjectStatus(CoroutineHandler* /*handler*/,
                                        const ObjectIdentifier& /*object_identifier*/,
                                        PageDbObjectStatus* /*object_status*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetObjectStatusKeys(coroutine::CoroutineHandler* /*handler*/,
                                            const ObjectDigest& /*object_digest*/,
                                            std::map<std::string, PageDbObjectStatus>* /*keys*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetInboundObjectReferences(coroutine::CoroutineHandler* /*handler*/,
                                                   const ObjectIdentifier& /*object_identifier*/,
                                                   ObjectReferencesAndPriority* /*references*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetInboundCommitReferences(coroutine::CoroutineHandler* /*handler*/,
                                                   const ObjectIdentifier& /*object_identifier*/,
                                                   std::vector<CommitId>* /*references*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::EnsureObjectDeletable(coroutine::CoroutineHandler* /*handler*/,
                                              const ObjectDigest& /*object_digest*/,
                                              std::vector<std::string>* /*object_status_keys*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::GetUnsyncedCommitIds(CoroutineHandler* /*handler*/,
                                             std::vector<CommitId>* /*commit_ids*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::IsCommitSynced(CoroutineHandler* /*handler*/, const CommitId& /*commit_id*/,
                                       bool* /*is_synced*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetUnsyncedPieces(CoroutineHandler* /*handler*/,
                                          std::vector<ObjectIdentifier>* /*object_identifiers*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::GetSyncMetadata(CoroutineHandler* /*handler*/, absl::string_view /*key*/,
                                        std::string* /*value*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::IsPageOnline(coroutine::CoroutineHandler* /*handler*/,
                                     bool* /*page_is_online*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddHead(CoroutineHandler* /*handler*/, CommitIdView /*head*/,
                                zx::time_utc /*timestamp*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::RemoveHead(CoroutineHandler* /*handler*/, CommitIdView /*head*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddMerge(coroutine::CoroutineHandler* /*handler*/,
                                 CommitIdView /*parent1_id*/, CommitIdView /*parent2_id*/,
                                 CommitIdView /*merge_commit_id*/) {
  return Status::ILLEGAL_STATE;
}
Status PageDbEmptyImpl::DeleteMerge(coroutine::CoroutineHandler* /*handler*/,
                                    CommitIdView /*parent1_id*/, CommitIdView /*parent2_id*/,
                                    CommitIdView /*commit_id*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::AddCommitStorageBytes(CoroutineHandler* /*handler*/,
                                              const CommitId& /*commit_id*/,
                                              absl::string_view /*remote_commit_id*/,
                                              const ObjectIdentifier& /*root_node*/,
                                              absl::string_view /*storage_bytes*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::DeleteCommit(coroutine::CoroutineHandler* /*handler*/,
                                     CommitIdView /*commit_id*/,
                                     absl::string_view /*remote_commit_id*/,
                                     const ObjectIdentifier& /*root_node*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::WriteObject(CoroutineHandler* /*handler*/, const Piece& /*piece*/,
                                    PageDbObjectStatus /*object_status*/,
                                    const ObjectReferencesAndPriority& /*children*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::DeleteObject(coroutine::CoroutineHandler* /*handler*/,
                                     const ObjectDigest& /*object_digest*/,
                                     const ObjectReferencesAndPriority& /*references*/) {
  return Status::NOT_IMPLEMENTED;
}
Status PageDbEmptyImpl::SetObjectStatus(CoroutineHandler* /*handler*/,
                                        const ObjectIdentifier& /*object_identifier*/,
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
Status PageDbEmptyImpl::SetSyncMetadata(CoroutineHandler* /*handler*/, absl::string_view /*key*/,
                                        absl::string_view /*value*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::MarkPageOnline(coroutine::CoroutineHandler* /*handlers*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::GetDeviceId(coroutine::CoroutineHandler* /*handler*/,
                                    clocks::DeviceId* /*device_id*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::GetClock(coroutine::CoroutineHandler* /*handler*/, Clock* /*clock*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::SetDeviceId(coroutine::CoroutineHandler* /*handler*/,
                                    const clocks::DeviceId& /*device_id*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::SetClock(coroutine::CoroutineHandler* /*handler*/, const Clock& /*entry*/) {
  return Status::NOT_IMPLEMENTED;
}

Status PageDbEmptyImpl::Execute(CoroutineHandler* /*handler*/) { return Status::NOT_IMPLEMENTED; }

}  // namespace storage
