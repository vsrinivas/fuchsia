// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/fit/function.h>
#include <lib/fsl/socket/strings.h>

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "src/ledger/bin/cloud_sync/impl/testing/test_commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"

namespace cloud_sync {
TestPageStorage::TestPageStorage(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

std::unique_ptr<TestCommit> TestPageStorage::NewCommit(std::string id,
                                                       std::string content,
                                                       bool unsynced) {
  auto commit = std::make_unique<TestCommit>(std::move(id), std::move(content));
  if (unsynced) {
    unsynced_commits_to_return.push_back(commit->Clone());
  }
  return commit;
}

storage::PageId TestPageStorage::GetId() { return page_id_to_return; }

void TestPageStorage::SetSyncDelegate(
    storage::PageSyncDelegate* page_sync_delegate) {
  page_sync_delegate_ = page_sync_delegate;
}

ledger::Status TestPageStorage::GetHeadCommits(
    std::vector<std::unique_ptr<const storage::Commit>>* head_commits) {
  *head_commits =
      std::vector<std::unique_ptr<const storage::Commit>>(head_count);
  return ledger::Status::OK;
}

void TestPageStorage::GetCommit(
    storage::CommitIdView commit_id,
    fit::function<void(ledger::Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  if (should_fail_get_commit) {
    async::PostTask(dispatcher_, [callback = std::move(callback)] {
      callback(ledger::Status::IO_ERROR, nullptr);
    });
    return;
  }

  async::PostTask(dispatcher_, [this, commit_id = commit_id.ToString(),
                                callback = std::move(callback)] {
    callback(ledger::Status::OK, std::move(new_commits_to_return[commit_id]));
  });
  new_commits_to_return.erase(commit_id.ToString());
}

void TestPageStorage::AddCommitsFromSync(
    std::vector<PageStorage::CommitIdAndBytes> ids_and_bytes,
    storage::ChangeSource /*source*/,
    fit::function<void(ledger::Status, std::vector<storage::CommitId>)>
        callback) {
  add_commits_from_sync_calls++;

  if (should_fail_add_commit_from_sync) {
    async::PostTask(dispatcher_, [callback = std::move(callback)]() {
      callback(ledger::Status::IO_ERROR, {});
    });
    return;
  }

  fit::closure confirm = [this, ids_and_bytes = std::move(ids_and_bytes),
                          callback = std::move(callback)]() mutable {
    for (auto& commit : ids_and_bytes) {
      received_commits[commit.id] = std::move(commit.bytes);
      unsynced_commits_to_return.erase(
          std::remove_if(
              unsynced_commits_to_return.begin(),
              unsynced_commits_to_return.end(),
              [commit_id = std::move(commit.id)](
                  const std::unique_ptr<const storage::Commit>& commit) {
                return commit->GetId() == commit_id;
              }),
          unsynced_commits_to_return.end());
    }
    async::PostTask(dispatcher_, [callback = std::move(callback)] {
      callback(ledger::Status::OK, {});
    });
  };
  if (should_delay_add_commit_confirmation) {
    delayed_add_commit_confirmations.push_back(std::move(confirm));
    return;
  }
  async::PostTask(dispatcher_, std::move(confirm));
}

void TestPageStorage::GetUnsyncedPieces(
    fit::function<void(ledger::Status, std::vector<storage::ObjectIdentifier>)>
        callback) {
  async::PostTask(dispatcher_, [callback = std::move(callback)] {
    callback(ledger::Status::OK, std::vector<storage::ObjectIdentifier>());
  });
}

void TestPageStorage::AddCommitWatcher(storage::CommitWatcher* watcher) {
  FXL_DCHECK(watcher_ == nullptr);
  watcher_ = watcher;
}

void TestPageStorage::RemoveCommitWatcher(storage::CommitWatcher* watcher) {
  FXL_DCHECK(watcher_ == watcher);
  watcher_ = nullptr;
}

void TestPageStorage::GetUnsyncedCommits(
    fit::function<void(ledger::Status,
                       std::vector<std::unique_ptr<const storage::Commit>>)>
        callback) {
  if (should_fail_get_unsynced_commits) {
    async::PostTask(dispatcher_, [callback = std::move(callback)] {
      callback(ledger::Status::IO_ERROR, {});
    });
    return;
  }
  std::vector<std::unique_ptr<const storage::Commit>> results;
  results.resize(unsynced_commits_to_return.size());
  std::transform(unsynced_commits_to_return.begin(),
                 unsynced_commits_to_return.end(), results.begin(),
                 [](const std::unique_ptr<const storage::Commit>& commit) {
                   return commit->Clone();
                 });
  async::PostTask(dispatcher_, [results = std::move(results),
                                callback = std::move(callback)]() mutable {
    callback(ledger::Status::OK, std::move(results));
  });
}

void TestPageStorage::MarkCommitSynced(
    const storage::CommitId& commit_id,
    fit::function<void(ledger::Status)> callback) {
  unsynced_commits_to_return.erase(
      std::remove_if(
          unsynced_commits_to_return.begin(), unsynced_commits_to_return.end(),
          [&commit_id](const std::unique_ptr<const storage::Commit>& commit) {
            return commit->GetId() == commit_id;
          }),
      unsynced_commits_to_return.end());
  commits_marked_as_synced.insert(commit_id);
  async::PostTask(dispatcher_, [callback = std::move(callback)] {
    callback(ledger::Status::OK);
  });
}

void TestPageStorage::SetSyncMetadata(
    fxl::StringView key, fxl::StringView value,
    fit::function<void(ledger::Status)> callback) {
  sync_metadata[key.ToString()] = value.ToString();
  async::PostTask(dispatcher_, [callback = std::move(callback)] {
    callback(ledger::Status::OK);
  });
}

void TestPageStorage::GetSyncMetadata(
    fxl::StringView key,
    fit::function<void(ledger::Status, std::string)> callback) {
  auto it = sync_metadata.find(key.ToString());
  if (it == sync_metadata.end()) {
    async::PostTask(dispatcher_, [callback = std::move(callback)] {
      callback(ledger::Status::INTERNAL_NOT_FOUND, "");
    });
    return;
  }
  async::PostTask(dispatcher_,
                  [callback = std::move(callback), metadata = it->second] {
                    callback(ledger::Status::OK, metadata);
                  });
}

}  // namespace cloud_sync
