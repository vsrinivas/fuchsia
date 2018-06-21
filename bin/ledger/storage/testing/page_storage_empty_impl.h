// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_TESTING_PAGE_STORAGE_EMPTY_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_TESTING_PAGE_STORAGE_EMPTY_IMPL_H_

#include <functional>
#include <memory>
#include <vector>

#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace storage {

// Empty implementaton of PageStorage. All methods do nothing and return dummy
// or empty responses.
class PageStorageEmptyImpl : public PageStorage {
 public:
  PageStorageEmptyImpl() = default;
  ~PageStorageEmptyImpl() override = default;

  // PageStorage:
  PageId GetId() override;

  void SetSyncDelegate(PageSyncDelegate* page_sync) override;

  void GetHeadCommitIds(
      std::function<void(Status, std::vector<CommitId>)> callback) override;

  void GetCommit(CommitIdView commit_id,
                 std::function<void(Status, std::unique_ptr<const Commit>)>
                     callback) override;

  void AddCommitsFromSync(std::vector<CommitIdAndBytes> ids_and_bytes,
                          ChangeSource source,
                          std::function<void(Status)> callback) override;

  void StartCommit(
      const CommitId& commit_id, JournalType journal_type,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) override;

  void StartMergeCommit(
      const CommitId& left, const CommitId& right,
      std::function<void(Status, std::unique_ptr<Journal>)> callback) override;

  void CommitJournal(
      std::unique_ptr<Journal> journal,
      std::function<void(Status, std::unique_ptr<const storage::Commit>)>
          callback) override;

  void RollbackJournal(std::unique_ptr<Journal> journal,
                       std::function<void(Status)> callback) override;

  Status AddCommitWatcher(CommitWatcher* watcher) override;

  Status RemoveCommitWatcher(CommitWatcher* watcher) override;

  void IsSynced(std::function<void(Status, bool)> callback) override;

  void GetUnsyncedCommits(
      std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
          callback) override;

  void MarkCommitSynced(const CommitId& commit_id,
                        std::function<void(Status)> callback) override;

  void GetUnsyncedPieces(
      std::function<void(Status, std::vector<ObjectIdentifier>)> callback)
      override;

  void MarkPieceSynced(ObjectIdentifier object_identifier,
                       std::function<void(Status)> callback) override;

  void IsPieceSynced(ObjectIdentifier object_identifier,
                     std::function<void(Status, bool)> callback) override;

  void AddObjectFromLocal(
      std::unique_ptr<DataSource> data_source,
      std::function<void(Status, ObjectIdentifier)> callback) override;

  void GetObject(ObjectIdentifier object_identifier, Location location,
                 std::function<void(Status, std::unique_ptr<const Object>)>
                     callback) override;

  void GetPiece(ObjectIdentifier object_identifier,
                std::function<void(Status, std::unique_ptr<const Object>)>
                    callback) override;

  void SetSyncMetadata(fxl::StringView key, fxl::StringView value,
                       std::function<void(Status)> callback) override;

  void GetSyncMetadata(
      fxl::StringView key,
      std::function<void(Status, std::string)> callback) override;

  void GetCommitContents(const Commit& commit, std::string min_key,
                         std::function<bool(Entry)> on_next,
                         std::function<void(Status)> on_done) override;

  void GetEntryFromCommit(const Commit& commit, std::string key,
                          std::function<void(Status, Entry)> callback) override;

  void GetCommitContentsDiff(const Commit& base_commit,
                             const Commit& other_commit, std::string min_key,
                             std::function<bool(EntryChange)> on_next_diff,
                             std::function<void(Status)> on_done) override;

  void GetThreeWayContentsDiff(const Commit& base_commit,
                               const Commit& left_commit,
                               const Commit& right_commit, std::string min_key,
                               std::function<bool(ThreeWayChange)> on_next_diff,
                               std::function<void(Status)> on_done) override;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_TESTING_PAGE_STORAGE_EMPTY_IMPL_H_
