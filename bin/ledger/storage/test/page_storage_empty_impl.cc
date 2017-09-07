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

void PageStorageEmptyImpl::SetSyncDelegate(PageSyncDelegate* /*page_sync*/) {
  FTL_NOTIMPLEMENTED();
}

void PageStorageEmptyImpl::GetHeadCommitIds(
    std::function<void(Status, std::vector<CommitId>)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, std::vector<CommitId>());
}

void PageStorageEmptyImpl::GetCommit(
    CommitIdView /*commit_id*/,
    std::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::AddCommitsFromSync(
    std::vector<CommitIdAndBytes> /*ids_and_bytes*/,
    std::function<void(Status)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::StartCommit(
    const CommitId& /*commit_id*/,
    JournalType /*journal_type*/,
    std::function<void(Status, std::unique_ptr<Journal>)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::StartMergeCommit(
    const CommitId& /*left*/,
    const CommitId& /*right*/,
    std::function<void(Status, std::unique_ptr<Journal>)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::CommitJournal(
    std::unique_ptr<Journal> /*journal*/,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

Status PageStorageEmptyImpl::RollbackJournal(
    std::unique_ptr<Journal> /*journal*/) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::AddCommitWatcher(CommitWatcher* /*watcher*/) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageEmptyImpl::RemoveCommitWatcher(CommitWatcher* /*watcher*/) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

void PageStorageEmptyImpl::GetUnsyncedCommits(
    std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
        callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, {});
}

void PageStorageEmptyImpl::MarkCommitSynced(
    const CommitId& /*commit_id*/,
    std::function<void(Status)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::GetUnsyncedPieces(
    std::function<void(Status, std::vector<ObjectId>)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, std::vector<ObjectId>());
}

void PageStorageEmptyImpl::MarkPieceSynced(
    ObjectIdView /*object_id*/,
    std::function<void(Status)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::AddObjectFromLocal(
    std::unique_ptr<DataSource> /*data_source*/,
    std::function<void(Status, ObjectId)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, "NOT_IMPLEMENTED");
}

void PageStorageEmptyImpl::GetObject(
    ObjectIdView /*object_id*/,
    Location /*location*/,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::GetPiece(
    ObjectIdView /*object_id*/,
    std::function<void(Status, std::unique_ptr<const Object>)> callback) {
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::SetSyncMetadata(
    ftl::StringView /*key*/,
    ftl::StringView /*value*/,
    std::function<void(Status)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

Status PageStorageEmptyImpl::GetSyncMetadata(ftl::StringView /*key*/,
                                             std::string* /*value*/) {
  FTL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

void PageStorageEmptyImpl::GetCommitContents(
    const Commit& /*commit*/,
    std::string /*min_key*/,
    std::function<bool(Entry)> /*on_next*/,
    std::function<void(Status)> on_done) {
  FTL_NOTIMPLEMENTED();
  on_done(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::GetEntryFromCommit(
    const Commit& /*commit*/,
    std::string /*key*/,
    std::function<void(Status, Entry)> callback) {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, Entry());
}

void PageStorageEmptyImpl::GetCommitContentsDiff(
    const Commit& /*base_commit*/,
    const Commit& /*other_commit*/,
    std::string /*min_key*/,
    std::function<bool(EntryChange)> /*on_next_diff*/,
    std::function<void(Status)> on_done) {
  FTL_NOTIMPLEMENTED();
  on_done(Status::NOT_IMPLEMENTED);
}

}  // namespace test
}  // namespace storage
