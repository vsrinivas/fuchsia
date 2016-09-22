// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/page_storage_impl.h"

namespace storage {

PageStorageImpl::PageStorageImpl(std::string page_path, PageId page_id)
    : page_path_(page_path), page_id_(page_id) {}

PageStorageImpl::~PageStorageImpl() {}

PageId PageStorageImpl::GetId() {
  return page_id_;
}

void PageStorageImpl::SetPageDeletionHandler(
    const std::function<void()>& on_page_deletion) {}

Status PageStorageImpl::GetHeadCommitIds(std::vector<CommitId>* commit_ids) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetCommit(const CommitId& commit_id,
                                  std::unique_ptr<Commit>* commit) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::AddCommitFromSync(const CommitId& id,
                                          const std::string& storage_bytes) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::StartCommit(const CommitId& commit_id,
                                    bool implicit,
                                    std::unique_ptr<Journal>* journal) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::StartMergeCommit(const CommitId& left,
                                         const CommitId& right,
                                         std::unique_ptr<Journal>* journal) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::AddCommitWatcher(CommitWatcher* watcher) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetUnsyncedCommits(
    std::vector<std::unique_ptr<Commit>>* commits) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::MarkCommitSynced(const CommitId& commit_id) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetDeltaObjects(const CommitId& commit_id,
                                        std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetUnsyncedObjects(const CommitId& commit_id,
                                           std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::MarkObjectSynced(const ObjectId& object_id) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::AddObjectFromSync(const ObjectId& object_id,
                                          mojo::DataPipeConsumerHandle data,
                                          size_t size) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::AddObjectFromLocal(mojo::DataPipeConsumerHandle data,
                                           size_t size,
                                           ObjectId* object_id) {
  return Status::NOT_IMPLEMENTED;
}

void PageStorageImpl::GetBlob(
    const ObjectId& blob_id,
    const std::function<void(Status status, std::unique_ptr<Blob> blob)>
        callback) {
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

}  // namespace storage
