// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/page_storage_impl.h"

#include "apps/ledger/storage/impl/commit_impl.h"
#include "apps/ledger/storage/public/constants.h"

namespace storage {

PageStorageImpl::PageStorageImpl(std::string page_path, PageIdView page_id)
    : page_path_(page_path), page_id_(page_id.ToString()), db_(page_path_) {}

PageStorageImpl::~PageStorageImpl() {}

Status PageStorageImpl::Init() {
  // Initialize DB.
  Status s = db_.Init();
  if (s != Status::OK) {
    return s;
  }

  // Add the default page head if this page is empty.
  std::vector<CommitId> heads;
  s = db_.GetHeads(&heads);
  if (s != Status::OK) {
    return s;
  }
  if (heads.empty()) {
    s = db_.AddHead(std::string(kFirstPageCommitId, kCommitIdSize));
    if (s != Status::OK) {
      return s;
    }
  }

  // Remove uncommited explicit journals.
  db_.RemoveExplicitJournals();
  // TODO(nellyv): Commit uncommited implicit journals.

  return Status::OK;
}

PageId PageStorageImpl::GetId() {
  return page_id_;
}

void PageStorageImpl::SetPageDeletionHandler(
    const std::function<void()>& on_page_deletion) {}

Status PageStorageImpl::GetHeadCommitIds(std::vector<CommitId>* commit_ids) {
  return db_.GetHeads(commit_ids);
}

Status PageStorageImpl::GetCommit(const CommitId& commit_id,
                                  std::unique_ptr<Commit>* commit) {
  std::string bytes;
  Status s = db_.GetCommitStorageBytes(commit_id, &bytes);
  if (s != Status::OK) {
    return s;
  }
  std::unique_ptr<Commit> c = CommitImpl::FromStorageBytes(commit_id, bytes);
  if (!c) {
    return Status::FORMAT_ERROR;
  }
  commit->swap(c);
  return Status::OK;
}

Status PageStorageImpl::AddCommitFromLocal(std::unique_ptr<Commit> commit) {
  return AddCommit(std::move(commit), ChangeSource::LOCAL);
}

Status PageStorageImpl::AddCommitFromSync(const CommitId& id,
                                          const std::string& storage_bytes) {
  std::unique_ptr<Commit> commit =
      CommitImpl::FromStorageBytes(id, storage_bytes);
  if (!commit) {
    return Status::FORMAT_ERROR;
  }
  return AddCommit(std::move(commit), ChangeSource::SYNC);
}

Status PageStorageImpl::StartCommit(const CommitId& commit_id,
                                    JournalType journal_type,
                                    std::unique_ptr<Journal>* journal) {
  return db_.CreateJournal(journal_type, commit_id, journal);
}

Status PageStorageImpl::StartMergeCommit(const CommitId& left,
                                         const CommitId& right,
                                         std::unique_ptr<Journal>* journal) {
  return db_.CreateMergeJournal(left, right, journal);
}

Status PageStorageImpl::AddCommitWatcher(CommitWatcher* watcher) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetUnsyncedCommits(
    std::vector<std::unique_ptr<Commit>>* commits) {
  std::vector<CommitId> resultIds;
  Status s = db_.GetUnsyncedCommitIds(&resultIds);
  if (s != Status::OK) {
    return s;
  }

  std::vector<std::unique_ptr<Commit>> result;
  for (size_t i = 0; i < resultIds.size(); ++i) {
    std::unique_ptr<Commit> commit;
    Status s = GetCommit(resultIds[i], &commit);
    if (s != Status::OK) {
      return s;
    }
    result.push_back(std::move(commit));
  }

  commits->swap(result);
  return Status::OK;
}

Status PageStorageImpl::MarkCommitSynced(const CommitId& commit_id) {
  return db_.MarkCommitIdSynced(commit_id);
}

Status PageStorageImpl::GetDeltaObjects(const CommitId& commit_id,
                                        std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetUnsyncedObjects(const CommitId& commit_id,
                                           std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::MarkObjectSynced(ObjectIdView object_id) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::AddObjectFromSync(
    ObjectIdView object_id,
    mojo::ScopedDataPipeConsumerHandle data,
    size_t size) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::AddObjectFromLocal(
    mojo::ScopedDataPipeConsumerHandle data,
    size_t size,
    ObjectId* object_id) {
  return Status::NOT_IMPLEMENTED;
}

void PageStorageImpl::GetBlob(
    ObjectIdView blob_id,
    const std::function<void(Status status, std::unique_ptr<Blob> blob)>
        callback) {
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

Status PageStorageImpl::AddCommit(std::unique_ptr<Commit> commit,
                                  ChangeSource source) {
  // TODO(nellyv): Update code to use a single transaction to do all the
  // following updates in the DB.
  Status s =
      db_.AddCommitStorageBytes(commit->GetId(), commit->GetStorageBytes());
  if (s != Status::OK) {
    return s;
  }

  if (source == ChangeSource::LOCAL) {
    s = db_.MarkCommitIdUnsynced(commit->GetId());
    if (s != Status::OK) {
      return s;
    }
  }

  // Update heads.
  s = db_.AddHead(commit->GetId());
  if (s != Status::OK) {
    return s;
  }

  // TODO(nellyv): Here we assume that commits arrive in order. Change this to
  // support out of order commit arrivals.
  // Remove parents from head (if they are in heads).
  for (const CommitId& parentId : commit->GetParentIds()) {
    db_.RemoveHead(parentId);
  }
  return Status::OK;
}

}  // namespace storage
