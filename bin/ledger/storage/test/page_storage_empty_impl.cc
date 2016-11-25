// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/test/page_storage_empty_impl.h"

#include "lib/ftl/logging.h"

namespace storage {
namespace test {

PageId PageStorageEmptyImpl::GetId() {
  FTL_NOTIMPLEMENTED();
  return "NOT_IMPLEMENTED";
}

void PageStorageEmptyImpl::SetSyncDelegate(PageSyncDelegate* page_sync) {
  FTL_NOTIMPLEMENTED();
}

Status PageStorageEmptyImpl::GetHeadCommitIds(
    std::vector<CommitId>* commit_ids) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::GetCommit(const CommitId& commit_id,
                                       std::unique_ptr<const Commit>* commit) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

void PageStorageEmptyImpl::AddCommitsFromSync(
    std::vector<CommitIdAndBytes> ids_and_bytes,
    std::function<void(Status)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

Status PageStorageEmptyImpl::StartCommit(const CommitId& commit_id,
                                         JournalType journal_type,
                                         std::unique_ptr<Journal>* journal) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::StartMergeCommit(
    const CommitId& left,
    const CommitId& right,
    std::unique_ptr<Journal>* journal) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::AddCommitWatcher(CommitWatcher* watcher) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::GetUnsyncedCommits(
    std::vector<std::unique_ptr<const Commit>>* commits) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::MarkCommitSynced(const CommitId& commit_id) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::GetDeltaObjects(const CommitId& commit_id,
                                             std::vector<ObjectId>* objects) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::GetUnsyncedObjects(
    const CommitId& commit_id,
    std::vector<ObjectId>* objects) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::MarkObjectSynced(ObjectIdView object_id) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

void PageStorageEmptyImpl::AddObjectFromSync(
    ObjectIdView object_id,
    mx::datapipe_consumer data,
    size_t size,
    const std::function<void(Status)>& callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::AddObjectFromLocal(
    mx::datapipe_consumer data,
    int64_t size,
    const std::function<void(Status, ObjectId)>& callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, "NOT_IMPLEMENTED");
}

void PageStorageEmptyImpl::GetObject(
    ObjectIdView object_id,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

Status PageStorageEmptyImpl::GetObjectSynchronous(
    ObjectIdView object_id,
    std::unique_ptr<const Object>* object) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::AddObjectSynchronous(
    convert::ExtendedStringView data,
    std::unique_ptr<const Object>* object) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::SetSyncMetadata(ftl::StringView sync_state) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::GetSyncMetadata(std::string* sync_state) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

}  // namespace test
}  // namespace storage
