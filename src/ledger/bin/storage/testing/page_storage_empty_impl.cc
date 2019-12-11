// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"

#include <lib/fit/function.h>

#include "src/ledger/lib/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

PageId PageStorageEmptyImpl::GetId() {
  LEDGER_NOTIMPLEMENTED();
  return "NOT_IMPLEMENTED";
}

ObjectIdentifierFactory* PageStorageEmptyImpl::GetObjectIdentifierFactory() {
  LEDGER_NOTIMPLEMENTED();
  return nullptr;
}

void PageStorageEmptyImpl::SetSyncDelegate(PageSyncDelegate* /*page_sync*/) {
  LEDGER_NOTIMPLEMENTED();
}

Status PageStorageEmptyImpl::GetHeadCommits(
    std::vector<std::unique_ptr<const Commit>>* head_commit) {
  LEDGER_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

void PageStorageEmptyImpl::GetMergeCommitIds(
    CommitIdView /*parent1_id*/, CommitIdView /*parent2_id*/,
    fit::function<void(Status, std::vector<CommitId>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, std::vector<CommitId>());
}

void PageStorageEmptyImpl::GetCommit(
    CommitIdView /*commit_id*/,
    fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::GetGenerationAndMissingParents(
    const CommitIdAndBytes& /*id_and_bytes*/,
    fit::function<void(Status, uint64_t, std::vector<CommitId>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, 0, {});
}

void PageStorageEmptyImpl::AddCommitsFromSync(std::vector<CommitIdAndBytes> /*ids_and_bytes*/,
                                              ChangeSource /*source*/,
                                              fit::function<void(Status)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

std::unique_ptr<Journal> PageStorageEmptyImpl::StartCommit(
    std::unique_ptr<const Commit> /*commit_id*/) {
  LEDGER_NOTIMPLEMENTED();
  return nullptr;
}

std::unique_ptr<Journal> PageStorageEmptyImpl::StartMergeCommit(
    std::unique_ptr<const Commit> /*left*/, std::unique_ptr<const Commit> /*right*/) {
  LEDGER_NOTIMPLEMENTED();
  return nullptr;
}

void PageStorageEmptyImpl::CommitJournal(
    std::unique_ptr<Journal> /*journal*/,
    fit::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::AddCommitWatcher(CommitWatcher* /*watcher*/) { LEDGER_NOTIMPLEMENTED(); }

void PageStorageEmptyImpl::RemoveCommitWatcher(CommitWatcher* /*watcher*/) {
  LEDGER_NOTIMPLEMENTED();
}

void PageStorageEmptyImpl::IsSynced(fit::function<void(Status, bool)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, false);
}

bool PageStorageEmptyImpl::IsOnline() {
  LEDGER_NOTIMPLEMENTED();
  return false;
}

void PageStorageEmptyImpl::IsEmpty(fit::function<void(Status, bool)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, false);
}

void PageStorageEmptyImpl::GetUnsyncedCommits(
    fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, {});
}

void PageStorageEmptyImpl::MarkCommitSynced(const CommitId& /*commit_id*/,
                                            fit::function<void(Status)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::GetUnsyncedPieces(
    fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, std::vector<ObjectIdentifier>());
}

void PageStorageEmptyImpl::MarkPieceSynced(ObjectIdentifier /*object_identifier*/,
                                           fit::function<void(Status)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::IsPieceSynced(ObjectIdentifier /*object_identifier*/,
                                         fit::function<void(Status, bool)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, false);
}

void PageStorageEmptyImpl::MarkSyncedToPeer(fit::function<void(Status)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::AddObjectFromLocal(
    ObjectType /*object_type*/, std::unique_ptr<DataSource> /*data_source*/,
    ObjectReferencesAndPriority references,
    fit::function<void(Status, ObjectIdentifier)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, {});
}

void PageStorageEmptyImpl::GetObjectPart(ObjectIdentifier object_identifier, int64_t offset,
                                         int64_t max_size, Location location,
                                         fit::function<void(Status, ledger::SizedVmo)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::GetObject(
    ObjectIdentifier /*object_identifier*/, Location /*location*/,
    fit::function<void(Status, std::unique_ptr<const Object>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::GetPiece(
    ObjectIdentifier /*object_identifier*/,
    fit::function<void(Status, std::unique_ptr<const Piece>)> callback) {
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

void PageStorageEmptyImpl::SetSyncMetadata(absl::string_view /*key*/, absl::string_view /*value*/,
                                           fit::function<void(Status)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::GetSyncMetadata(absl::string_view /*key*/,
                                           fit::function<void(Status, std::string)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, "");
}

void PageStorageEmptyImpl::GetCommitContents(const Commit& /*commit*/, std::string /*min_key*/,
                                             fit::function<bool(Entry)> /*on_next*/,
                                             fit::function<void(Status)> on_done) {
  LEDGER_NOTIMPLEMENTED();
  on_done(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::GetEntryFromCommit(const Commit& /*commit*/, std::string /*key*/,
                                              fit::function<void(Status, Entry)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, Entry());
}

void PageStorageEmptyImpl::GetDiffForCloud(
    const Commit& /*target_commit*/,
    fit::function<void(Status, CommitIdView, std::vector<EntryChange>)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, "", {});
}

void PageStorageEmptyImpl::GetCommitContentsDiff(const Commit& /*base_commit*/,
                                                 const Commit& /*other_commit*/,
                                                 std::string /*min_key*/,
                                                 fit::function<bool(EntryChange)> /*on_next_diff*/,
                                                 fit::function<void(Status)> on_done) {
  LEDGER_NOTIMPLEMENTED();
  on_done(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::GetThreeWayContentsDiff(
    const Commit& /*base_commit*/, const Commit& /*left_commit*/, const Commit& /*right_commit*/,
    std::string /*min_key*/, fit::function<bool(ThreeWayChange)> /*on_next_diff*/,
    fit::function<void(Status)> on_done) {
  LEDGER_NOTIMPLEMENTED();
  on_done(Status::NOT_IMPLEMENTED);
}

void PageStorageEmptyImpl::GetClock(fit::function<void(Status, Clock)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, {});
}

void PageStorageEmptyImpl::GetCommitIdFromRemoteId(absl::string_view remote_commit_id,
                                                   fit::function<void(Status, CommitId)> callback) {
  LEDGER_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, "");
}

}  // namespace storage
