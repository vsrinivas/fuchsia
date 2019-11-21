// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_TESTING_PAGE_STORAGE_EMPTY_IMPL_H_
#define SRC_LEDGER_BIN_STORAGE_TESTING_PAGE_STORAGE_EMPTY_IMPL_H_

#include <lib/fit/function.h>

#include <functional>
#include <memory>
#include <vector>

#include "src/ledger/bin/storage/public/page_storage.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

// Empty implementaton of PageStorage. All methods do nothing and return dummy
// or empty responses.
class PageStorageEmptyImpl : public PageStorage {
 public:
  PageStorageEmptyImpl() = default;
  ~PageStorageEmptyImpl() override = default;

  // PageStorage:
  PageId GetId() override;

  ObjectIdentifierFactory* GetObjectIdentifierFactory() override;

  void SetSyncDelegate(PageSyncDelegate* page_sync) override;

  Status GetHeadCommits(std::vector<std::unique_ptr<const Commit>>* head_commits) override;

  void GetMergeCommitIds(CommitIdView parent1_id, CommitIdView parent2_id,
                         fit::function<void(Status, std::vector<CommitId>)> callback) override;

  void GetCommit(CommitIdView commit_id,
                 fit::function<void(Status, std::unique_ptr<const Commit>)> callback) override;

  void GetGenerationAndMissingParents(
      const CommitIdAndBytes& id_and_bytes,
      fit::function<void(Status, uint64_t, std::vector<CommitId>)> callback) override;

  void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes, ChangeSource source,
                          fit::function<void(Status)> callback) override;

  std::unique_ptr<Journal> StartCommit(std::unique_ptr<const Commit> commit_id) override;

  std::unique_ptr<Journal> StartMergeCommit(std::unique_ptr<const Commit> left,
                                            std::unique_ptr<const Commit> right) override;

  void CommitJournal(std::unique_ptr<Journal> journal,
                     fit::function<void(Status, std::unique_ptr<const Commit>)> callback) override;

  void AddCommitWatcher(CommitWatcher* watcher) override;

  void RemoveCommitWatcher(CommitWatcher* watcher) override;

  void IsSynced(fit::function<void(Status, bool)> callback) override;

  bool IsOnline() override;

  void IsEmpty(fit::function<void(Status, bool)> callback) override;

  void GetUnsyncedCommits(
      fit::function<void(Status, std::vector<std::unique_ptr<const Commit>>)> callback) override;

  void MarkCommitSynced(const CommitId& commit_id, fit::function<void(Status)> callback) override;

  void GetUnsyncedPieces(
      fit::function<void(Status, std::vector<ObjectIdentifier>)> callback) override;

  void MarkPieceSynced(ObjectIdentifier object_identifier,
                       fit::function<void(Status)> callback) override;

  void IsPieceSynced(ObjectIdentifier object_identifier,
                     fit::function<void(Status, bool)> callback) override;

  void MarkSyncedToPeer(fit::function<void(Status)> callback) override;

  void AddObjectFromLocal(ObjectType object_type, std::unique_ptr<DataSource> data_source,
                          ObjectReferencesAndPriority references,
                          fit::function<void(Status, ObjectIdentifier)> callback) override;

  void GetObjectPart(ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
                     Location location,
                     fit::function<void(Status, ledger::SizedVmo)> callback) override;

  void GetObject(ObjectIdentifier object_identifier, Location location,
                 fit::function<void(Status, std::unique_ptr<const Object>)> callback) override;

  void GetPiece(ObjectIdentifier object_identifier,
                fit::function<void(Status, std::unique_ptr<const Piece>)> callback) override;

  void SetSyncMetadata(absl::string_view key, absl::string_view value,
                       fit::function<void(Status)> callback) override;

  void GetSyncMetadata(absl::string_view key,
                       fit::function<void(Status, std::string)> callback) override;

  void GetCommitContents(const Commit& commit, std::string min_key,
                         fit::function<bool(Entry)> on_next,
                         fit::function<void(Status)> on_done) override;

  void GetEntryFromCommit(const Commit& commit, std::string key,
                          fit::function<void(Status, Entry)> callback) override;

  void GetDiffForCloud(
      const Commit& target_commit,
      fit::function<void(Status, CommitIdView, std::vector<EntryChange>)> callback) override;

  void GetCommitContentsDiff(const Commit& base_commit, const Commit& other_commit,
                             std::string min_key, fit::function<bool(EntryChange)> on_next_diff,
                             fit::function<void(Status)> on_done) override;

  void GetThreeWayContentsDiff(const Commit& base_commit, const Commit& left_commit,
                               const Commit& right_commit, std::string min_key,
                               fit::function<bool(ThreeWayChange)> on_next_diff,
                               fit::function<void(Status)> on_done) override;

  void GetClock(fit::function<void(Status, Clock)> callback) override;

  void GetCommitIdFromRemoteId(absl::string_view remote_commit_id,
                               fit::function<void(Status, CommitId)> callback) override;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_TESTING_PAGE_STORAGE_EMPTY_IMPL_H_
