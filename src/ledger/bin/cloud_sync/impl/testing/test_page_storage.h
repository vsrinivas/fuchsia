// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_STORAGE_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_STORAGE_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "src/ledger/bin/cloud_sync/impl/testing/test_commit.h"
#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/lib/socket/strings.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace cloud_sync {
// Fake implementation of storage::PageStorage. Injects the data that PageSync
// asks about: page id and existing unsynced commits to be retrieved through
// GetUnsyncedCommits(). Registers the commits marked as synced.
// TODO(LE-829): migrate to storage::fake::FakePageStorage.
class TestPageStorage : public storage::PageStorageEmptyImpl {
 public:
  explicit TestPageStorage(async_dispatcher_t* dispatcher);

  std::unique_ptr<TestCommit> NewCommit(std::string id, std::string content, bool unsynced = true);

  // storage::PageStorage:
  storage::PageId GetId() override;
  storage::ObjectIdentifierFactory* GetObjectIdentifierFactory() override;
  void SetSyncDelegate(storage::PageSyncDelegate* page_sync_delegate) override;
  ledger::Status GetHeadCommits(
      std::vector<std::unique_ptr<const storage::Commit>>* head_commits) override;
  void AddCommitsFromSync(std::vector<PageStorage::CommitIdAndBytes> ids_and_bytes,
                          storage::ChangeSource source,
                          fit::function<void(ledger::Status)> callback) override;
  void GetUnsyncedPieces(fit::function<void(ledger::Status, std::vector<storage::ObjectIdentifier>)>
                             callback) override;
  void AddCommitWatcher(storage::CommitWatcher* watcher) override;
  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override;
  void GetUnsyncedCommits(
      fit::function<void(ledger::Status, std::vector<std::unique_ptr<const storage::Commit>>)>
          callback) override;
  void MarkCommitSynced(const storage::CommitId& commit_id,
                        fit::function<void(ledger::Status)> callback) override;
  void MarkPieceSynced(storage::ObjectIdentifier object_identifier,
                       fit::function<void(ledger::Status)> callback) override;
  void SetSyncMetadata(absl::string_view key, absl::string_view value,
                       fit::function<void(ledger::Status)> callback) override;
  void GetSyncMetadata(absl::string_view key,
                       fit::function<void(ledger::Status, std::string)> callback) override;
  void GetObject(storage::ObjectIdentifier object_identifier, Location /*location*/,
                 fit::function<void(ledger::Status, std::unique_ptr<const storage::Object>)>
                     callback) override;
  void GetPiece(
      storage::ObjectIdentifier object_identifier,
      fit::function<void(ledger::Status, std::unique_ptr<const storage::Piece>)> callback) override;
  void GetDiffForCloud(
      const storage::Commit& commit,
      fit::function<void(ledger::Status, storage::CommitIdView, std::vector<storage::EntryChange>)>
          callback) override;
  void GetCommitIdFromRemoteId(
      absl::string_view remote_commit_id,
      fit::function<void(ledger::Status, storage::CommitId)> callback) override;

  storage::PageId page_id_to_return;
  // Commits to be returned from GetUnsyncedCommits calls.
  std::vector<std::unique_ptr<const storage::Commit>> unsynced_commits_to_return;
  // Diffs to be returned from GetDiffForCloud.
  std::map<storage::CommitId, std::pair<storage::CommitId, std::vector<storage::EntryChange>>>
      diffs_to_return;
  // Objects to be returned from GetUnsyncedPieces/GetObject calls.
  std::map<storage::ObjectIdentifier, std::unique_ptr<const storage::fake::FakePiece>>
      unsynced_objects_to_return;
  // Mapping from remote commit ids to local ids.
  std::map<std::string, storage::CommitId, std::less<>> remote_id_to_commit_id;
  size_t head_count = 1;
  bool should_fail_get_unsynced_commits = false;
  bool should_fail_get_unsynced_pieces = false;
  bool should_fail_add_commit_from_sync = false;
  bool should_delay_add_commit_confirmation = false;
  bool should_fail_get_diff_for_cloud = false;
  bool should_fail_mark_piece_synced = false;
  std::vector<fit::closure> delayed_add_commit_confirmations;

  unsigned int add_commits_from_sync_calls = 0u;
  unsigned int get_unsynced_commits_calls = 0u;

  storage::PageSyncDelegate* page_sync_delegate_;
  std::set<storage::CommitId> commits_marked_as_synced;
  std::set<storage::ObjectIdentifier> objects_marked_as_synced;
  storage::CommitWatcher* watcher_ = nullptr;
  bool watcher_set = false;
  bool watcher_removed = false;
  std::map<storage::CommitId, std::string> received_commits;

  bool ReceivedCommitsContains(convert::ExtendedStringView content);

  std::map<std::string, std::string> sync_metadata;
  storage::fake::FakeObjectIdentifierFactory object_identifier_factory_;

 private:
  async_dispatcher_t* const dispatcher_;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_PAGE_STORAGE_H_
