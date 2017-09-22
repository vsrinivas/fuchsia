// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/page_sync_impl.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "apps/ledger/src/auth_provider/test/test_auth_provider.h"
#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/cloud_provider/test/test_page_cloud_handler.h"
#include "apps/ledger/src/cloud_sync/impl/constants.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/test/commit_empty_impl.h"
#include "apps/ledger/src/storage/test/page_storage_empty_impl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"

namespace cloud_sync {
namespace {

// Fake implementation of storage::Commit.
class TestCommit : public storage::test::CommitEmptyImpl {
 public:
  TestCommit() = default;
  TestCommit(storage::CommitId id, std::string content)
      : id(std::move(id)), content(std::move(content)) {}
  ~TestCommit() override = default;

  std::vector<std::unique_ptr<const Commit>> AsList() {
    std::vector<std::unique_ptr<const Commit>> result;
    result.push_back(Clone());
    return result;
  }

  std::unique_ptr<Commit> Clone() const override {
    return std::make_unique<TestCommit>(id, content);
  }

  const storage::CommitId& GetId() const override { return id; }

  fxl::StringView GetStorageBytes() const override { return content; }

  storage::CommitId id;
  std::string content;
};

// Fake implementation of storage::PageStorage. Injects the data that PageSync
// asks about: page id, existing unsynced commits to be retrieved through
// GetUnsyncedCommits() and new commits to be retrieved through GetCommit().
// Registers the commits marked as synced.
class TestPageStorage : public storage::test::PageStorageEmptyImpl {
 public:
  explicit TestPageStorage(fsl::MessageLoop* message_loop)
      : message_loop_(message_loop) {}

  std::unique_ptr<TestCommit> NewCommit(std::string id,
                                        std::string content,
                                        bool unsynced = true) {
    auto commit =
        std::make_unique<TestCommit>(std::move(id), std::move(content));
    if (unsynced) {
      unsynced_commits_to_return.push_back(commit->Clone());
    }
    return commit;
  }

  storage::PageId GetId() override { return page_id_to_return; }

  void SetSyncDelegate(storage::PageSyncDelegate* /*page_sync*/) override {}

  void GetHeadCommitIds(
      std::function<void(storage::Status, std::vector<storage::CommitId>)>
          callback) override {
    message_loop_->task_runner()->PostTask(
        [ this, callback = std::move(callback) ] {
          // Current tests only rely on the number of heads, not on the actual
          // ids.
          callback(storage::Status::OK,
                   std::vector<storage::CommitId>(head_count));
        });
  }

  void GetCommit(storage::CommitIdView commit_id,
                 std::function<void(storage::Status,
                                    std::unique_ptr<const storage::Commit>)>
                     callback) override {
    if (should_fail_get_commit) {
      callback(storage::Status::IO_ERROR, nullptr);
      return;
    }

    callback(storage::Status::OK,
             std::move(new_commits_to_return[commit_id.ToString()]));
    new_commits_to_return.erase(commit_id.ToString());
  }

  void AddCommitsFromSync(
      std::vector<PageStorage::CommitIdAndBytes> ids_and_bytes,
      std::function<void(storage::Status status)> callback) override {
    add_commits_from_sync_calls++;

    if (should_fail_add_commit_from_sync) {
      message_loop_->task_runner()->PostTask(
          [callback]() { callback(storage::Status::IO_ERROR); });
      return;
    }

    fxl::Closure confirm = fxl::MakeCopyable(
        [ this, ids_and_bytes = std::move(ids_and_bytes), callback ]() mutable {
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
          callback(storage::Status::OK);
        });
    if (should_delay_add_commit_confirmation) {
      delayed_add_commit_confirmations.push_back(move(confirm));
      return;
    }
    message_loop_->task_runner()->PostTask(confirm);
  }

  void GetUnsyncedPieces(
      std::function<void(storage::Status, std::vector<storage::ObjectId>)>
          callback) override {
    callback(storage::Status::OK, std::vector<storage::ObjectId>());
  }

  storage::Status AddCommitWatcher(
      storage::CommitWatcher* /*watcher*/) override {
    watcher_set = true;
    return storage::Status::OK;
  }

  storage::Status RemoveCommitWatcher(
      storage::CommitWatcher* /*watcher*/) override {
    watcher_removed = true;
    return storage::Status::OK;
  }

  void GetUnsyncedCommits(
      std::function<void(storage::Status,
                         std::vector<std::unique_ptr<const storage::Commit>>)>
          callback) override {
    if (should_fail_get_unsynced_commits) {
      callback(storage::Status::IO_ERROR, {});
      return;
    }
    std::vector<std::unique_ptr<const storage::Commit>> results;
    results.resize(unsynced_commits_to_return.size());
    std::transform(unsynced_commits_to_return.begin(),
                   unsynced_commits_to_return.end(), results.begin(),
                   [](const std::unique_ptr<const storage::Commit>& commit) {
                     return commit->Clone();
                   });
    callback(storage::Status::OK, std::move(results));
  }

  void MarkCommitSynced(
      const storage::CommitId& commit_id,
      std::function<void(storage::Status)> callback) override {
    unsynced_commits_to_return.erase(
        std::remove_if(
            unsynced_commits_to_return.begin(),
            unsynced_commits_to_return.end(),
            [&commit_id](const std::unique_ptr<const storage::Commit>& commit) {
              return commit->GetId() == commit_id;
            }),
        unsynced_commits_to_return.end());
    commits_marked_as_synced.insert(commit_id);
    callback(storage::Status::OK);
  }

  void SetSyncMetadata(fxl::StringView key,
                       fxl::StringView value,
                       std::function<void(storage::Status)> callback) override {
    sync_metadata[key.ToString()] = value.ToString();
    callback(storage::Status::OK);
  }

  void GetSyncMetadata(
      fxl::StringView key,
      std::function<void(storage::Status, std::string)> callback) override {
    auto it = sync_metadata.find(key.ToString());
    if (it == sync_metadata.end()) {
      callback(storage::Status::NOT_FOUND, "");
      return;
    }
    callback(storage::Status::OK, it->second);
  }

  storage::PageId page_id_to_return;
  // Commits to be returned from GetUnsyncedCommits calls.
  std::vector<std::unique_ptr<const storage::Commit>>
      unsynced_commits_to_return;
  size_t head_count = 1;
  // Commits to be returned from GetCommit() calls.
  std::unordered_map<storage::CommitId, std::unique_ptr<const storage::Commit>>
      new_commits_to_return;
  bool should_fail_get_unsynced_commits = false;
  bool should_fail_get_commit = false;
  bool should_fail_add_commit_from_sync = false;
  bool should_delay_add_commit_confirmation = false;
  std::vector<fxl::Closure> delayed_add_commit_confirmations;
  unsigned int add_commits_from_sync_calls = 0u;

  std::set<storage::CommitId> commits_marked_as_synced;
  bool watcher_set = false;
  bool watcher_removed = false;
  std::unordered_map<storage::CommitId, std::string> received_commits;
  std::unordered_map<std::string, std::string> sync_metadata;

 private:
  fsl::MessageLoop* message_loop_;
};

// Dummy implementation of a backoff policy, which always returns zero backoff
// time..
class TestBackoff : public backoff::Backoff {
 public:
  explicit TestBackoff(int* get_next_count) : get_next_count_(get_next_count) {}
  ~TestBackoff() override {}

  fxl::TimeDelta GetNext() override {
    (*get_next_count_)++;
    return fxl::TimeDelta::FromMilliseconds(50);
  }

  void Reset() override {}

  int* get_next_count_;
};

class TestSyncStateWatcher : public SyncStateWatcher {
 public:
  TestSyncStateWatcher() {}
  ~TestSyncStateWatcher() override{};

  void Notify(SyncStateContainer sync_state) override {
    if (!states.empty() && sync_state == *states.rbegin()) {
      return;
    }
    states.push_back(sync_state);
  }

  std::vector<SyncStateContainer> states;
};

class PageSyncImplTest : public ::test::TestWithMessageLoop {
 public:
  PageSyncImplTest()
      : storage_(&message_loop_),
        cloud_provider_(message_loop_.task_runner()),
        auth_provider_(message_loop_.task_runner()) {
    std::unique_ptr<TestSyncStateWatcher> watcher =
        std::make_unique<TestSyncStateWatcher>();
    state_watcher_ = watcher.get();
    page_sync_ = std::make_unique<PageSyncImpl>(
        message_loop_.task_runner(), &storage_, &cloud_provider_,
        &auth_provider_,
        std::make_unique<TestBackoff>(&backoff_get_next_calls_),
        [this] {
          error_callback_calls_++;
          message_loop_.PostQuitTask();
        },
        std::move(watcher));
  }
  ~PageSyncImplTest() override {}

 protected:
  enum class UploadStatus {
    ENABLED,
    DISABLED,
  };
  void StartPageSync(UploadStatus status = UploadStatus::ENABLED) {
    if (status == UploadStatus::ENABLED) {
      page_sync_->EnableUpload();
    }
    page_sync_->Start();
  }

  TestPageStorage storage_;
  cloud_provider_firebase::test::TestPageCloudHandler cloud_provider_;
  auth_provider::test::TestAuthProvider auth_provider_;
  int backoff_get_next_calls_ = 0;
  TestSyncStateWatcher* state_watcher_;
  std::unique_ptr<PageSyncImpl> page_sync_;
  int error_callback_calls_ = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageSyncImplTest);
};

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to PageCloudHandler.
TEST_F(PageSyncImplTest, UploadBacklog) {
  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");
  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();

  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id1", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id2", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content2", cloud_provider_.received_commits[1].content);
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));

  ASSERT_EQ(5u, state_watcher_->states.size());
  EXPECT_EQ(CATCH_UP_DOWNLOAD, state_watcher_->states[0].download);
  EXPECT_EQ(DOWNLOAD_IDLE, state_watcher_->states[1].download);
  EXPECT_EQ(DOWNLOAD_IDLE, state_watcher_->states[2].download);
  EXPECT_EQ(DOWNLOAD_IDLE, state_watcher_->states[3].download);
  EXPECT_EQ(DOWNLOAD_IDLE, state_watcher_->states[4].download);

  EXPECT_EQ(WAIT_CATCH_UP_DOWNLOAD, state_watcher_->states[0].upload);
  EXPECT_EQ(WAIT_CATCH_UP_DOWNLOAD, state_watcher_->states[1].upload);
  EXPECT_EQ(UPLOAD_PENDING, state_watcher_->states[2].upload);
  EXPECT_EQ(UPLOAD_IN_PROGRESS, state_watcher_->states[3].upload);
  EXPECT_EQ(UPLOAD_IDLE, state_watcher_->states[4].upload);
}

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to PageCloudHandler.
TEST_F(PageSyncImplTest, PageWatcher) {
  TestSyncStateWatcher watcher;
  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");
  page_sync_->SetOnIdle(MakeQuitTask());
  page_sync_->SetSyncWatcher(&watcher);
  StartPageSync();

  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(6u, watcher.states.size());
  EXPECT_EQ(DOWNLOAD_IDLE, watcher.states[0].download);
  EXPECT_EQ(CATCH_UP_DOWNLOAD, watcher.states[1].download);
  EXPECT_EQ(DOWNLOAD_IDLE, watcher.states[2].download);
  EXPECT_EQ(DOWNLOAD_IDLE, watcher.states[3].download);
  EXPECT_EQ(DOWNLOAD_IDLE, watcher.states[4].download);
  EXPECT_EQ(DOWNLOAD_IDLE, watcher.states[5].download);

  EXPECT_EQ(UPLOAD_IDLE, watcher.states[0].upload);
  EXPECT_EQ(WAIT_CATCH_UP_DOWNLOAD, watcher.states[1].upload);
  EXPECT_EQ(WAIT_CATCH_UP_DOWNLOAD, watcher.states[2].upload);
  EXPECT_EQ(UPLOAD_PENDING, watcher.states[3].upload);
  EXPECT_EQ(UPLOAD_IN_PROGRESS, watcher.states[4].upload);
  EXPECT_EQ(UPLOAD_IDLE, watcher.states[5].upload);
}

// Verifies that the backlog of commits to upload is not uploaded until there's
// only one local head.
TEST_F(PageSyncImplTest, UploadBacklogOnlyOnSingleHead) {
  // Verify that two local commits are not uploaded when there is two local
  // heads.
  storage_.head_count = 2;
  storage_.NewCommit("id0", "content0");
  storage_.NewCommit("id1", "content1");
  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());

  // Add a new commit and reduce the number of heads to 1.
  storage_.head_count = 1;
  auto commit = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit->Clone();
  page_sync_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Verify that all local commits were uploaded.
  ASSERT_EQ(3u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id0", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content0", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id1", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[1].content);
  EXPECT_EQ("id2", cloud_provider_.received_commits[2].id);
  EXPECT_EQ("content2", cloud_provider_.received_commits[2].content);
  EXPECT_EQ(3u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id0"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

// Verifies that sync pauses uploading commits when it is downloading a commit.
TEST_F(PageSyncImplTest, NoUploadWhenDownloading) {
  storage_.should_delay_add_commit_confirmation = true;

  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());
  std::vector<cloud_provider_firebase::Record> records;
  records.emplace_back(cloud_provider_firebase::Commit("id1", "content1"),
                       "44");
  page_sync_->OnRemoteCommits(std::move(records));
  page_sync_->OnNewCommits(storage_.NewCommit("id2", "content2")->AsList(),
                           storage::ChangeSource::LOCAL);

  EXPECT_FALSE(storage_.delayed_add_commit_confirmations.empty());
  EXPECT_TRUE(cloud_provider_.received_commits.empty());

  storage_.delayed_add_commit_confirmations.front()();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(cloud_provider_.received_commits.empty());
}

TEST_F(PageSyncImplTest, UploadExistingCommitsOnlyAfterBacklogDownload) {
  // Verify that two local commits are not uploaded when there is two local
  // heads.
  storage_.NewCommit("local1", "content1");
  storage_.NewCommit("local2", "content2");

  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("remote3", "content3"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("remote4", "content4"), "43");
  bool backlog_downloaded_called = false;
  page_sync_->SetOnBacklogDownloaded([this, &backlog_downloaded_called] {
    EXPECT_EQ(0u, cloud_provider_.received_commits.size());
    EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
    backlog_downloaded_called = true;
  });
  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(backlog_downloaded_called);
  ASSERT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("local1", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("local2", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content2", cloud_provider_.received_commits[1].content);
  ASSERT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("local1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("local2"));
}

// Verfies that the new commits that PageSync is notified about through storage
// watcher are uploaded to PageCloudHandler, with the exception of commits that
// themselves come from sync.
TEST_F(PageSyncImplTest, UploadNewCommits) {
  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  page_sync_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);

  // The commit coming from sync should be ignored.
  auto commit2 = storage_.NewCommit("id2", "content2", false);
  storage_.new_commits_to_return["id2"] = commit2->Clone();
  page_sync_->OnNewCommits(commit2->AsList(), storage::ChangeSource::SYNC);

  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.new_commits_to_return["id3"] = commit3->Clone();
  page_sync_->OnNewCommits(commit3->AsList(), storage::ChangeSource::LOCAL);

  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id1", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id3", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content3", cloud_provider_.received_commits[1].content);
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id3"));
}

// Verifies that new commits being added to storage are only uploaded while
// there is only a single head.
TEST_F(PageSyncImplTest, UploadNewCommitsOnlyOnSingleHead) {
  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  // Add a new commit when there's only one head and verify that it is
  // uploaded.
  storage_.head_count = 1;
  auto commit0 = storage_.NewCommit("id0", "content0");
  storage_.new_commits_to_return["id0"] = commit0->Clone();
  page_sync_->OnNewCommits(commit0->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_sync_->IsIdle());
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(1u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id0", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content0", cloud_provider_.received_commits[0].content);
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id0"));

  // Add another commit when there's two heads and verify that it is not
  // uploaded.
  cloud_provider_.received_commits.clear();
  storage_.head_count = 2;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  page_sync_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(0u, cloud_provider_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.count("id1"));

  // Add another commit bringing the number of heads down to one and verify that
  // both commits are uploaded.
  storage_.head_count = 1;
  auto commit2 = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit2->Clone();
  page_sync_->OnNewCommits(commit2->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_sync_->IsIdle());
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id1", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id2", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content2", cloud_provider_.received_commits[1].content);
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

// Verifies that existing commits are uploaded before the new ones.
TEST_F(PageSyncImplTest, UploadExistingAndNewCommits) {
  storage_.NewCommit("id1", "content1");

  page_sync_->SetOnBacklogDownloaded([this] {
    message_loop_.task_runner()->PostTask([this] {
      auto commit = storage_.NewCommit("id2", "content2");
      storage_.new_commits_to_return["id2"] = commit->Clone();
      page_sync_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
    });
  });
  page_sync_->SetOnIdle(MakeQuitTask());

  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id1", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id2", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content2", cloud_provider_.received_commits[1].content);
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

// Verifies that failing uploads are retried. In production the retries are
// delayed, here we set the delays to 0.
TEST_F(PageSyncImplTest, RetryUpload) {
  // Complete the initial sync.
  StartPageSync();
  EXPECT_TRUE(
      RunLoopUntil([this] { return cloud_provider_.get_commits_calls > 0u; }));

  // Add a new local commit, but set the cloud_provider to fail.
  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  page_sync_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);

  // Test cloud provider logs every commit, even if it reports that upload
  // failed for each. Here we loop through at least five attempts to upload the
  // commit.
  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.add_commits_calls >= 5u &&
           // We need to wait for the callback to be executed on the PageSync
           // side.
           backoff_get_next_calls_ >= 5;
  }));
  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_GE(backoff_get_next_calls_, 5);
}

// Verifies that the on idle callback is called when there is no pending upload
// tasks.
TEST_F(PageSyncImplTest, UploadIdleCallback) {
  int on_idle_calls = 0;

  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");

  page_sync_->SetOnIdle([this, &on_idle_calls] {
    on_idle_calls++;
    message_loop_.PostQuitTask();
  });
  StartPageSync();

  // Verify that the idle callback is called once both commits are uploaded.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_sync_->IsIdle());

  // Notify about a new commit to upload and verify that the idle callback was
  // called again on completion.
  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.new_commits_to_return["id3"] = commit3->Clone();
  page_sync_->OnNewCommits(commit3->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_sync_->IsIdle());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(3u, cloud_provider_.received_commits.size());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_sync_->IsIdle());
}

// Verifies that if listing the original commits to be uploaded fails, the
// client is notified about the error and the storage watcher is never set, so
// that subsequent commits are not handled. (as this would violate the contract
// of uploading commits in order)
TEST_F(PageSyncImplTest, FailToListCommits) {
  EXPECT_FALSE(storage_.watcher_set);
  EXPECT_EQ(0, error_callback_calls_);
  storage_.should_fail_get_unsynced_commits = true;
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, error_callback_calls_);
  EXPECT_FALSE(storage_.watcher_set);
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
}

// Verifies that the backlog of unsynced commits is retrieved from the cloud
// provider and saved in storage.
TEST_F(PageSyncImplTest, DownloadBacklog) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));

  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");

  int on_backlog_downloaded_calls = 0;
  page_sync_->SetOnBacklogDownloaded([this, &on_backlog_downloaded_calls] {
    on_backlog_downloaded_calls++;
    message_loop_.PostQuitTask();
  });
  auth_provider_.token_to_return = "some-token";
  StartPageSync();

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata[kTimestampKey.ToString()]);
  EXPECT_EQ(1, on_backlog_downloaded_calls);
  EXPECT_EQ(std::vector<std::string>{"some-token"},
            cloud_provider_.get_commits_auth_tokens);
}

// Verifies that if auth provider fails to provide the auth token, the error
// callback is called.
TEST_F(PageSyncImplTest, DownloadBacklogAuthError) {
  auth_provider_.status_to_return = auth_provider::AuthStatus::ERROR;
  auth_provider_.token_to_return = "";
  EXPECT_EQ(0, error_callback_calls_);
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1, error_callback_calls_);
  EXPECT_EQ(std::vector<std::string>{},
            cloud_provider_.get_commits_auth_tokens);
}

// Verifies that callbacks are correctly run after downloading an empty backlog
// of remote commits.
TEST_F(PageSyncImplTest, DownloadEmptyBacklog) {
  int on_backlog_downloaded_calls = 0;
  int on_idle_calls = 0;
  page_sync_->SetOnBacklogDownloaded(
      [&on_backlog_downloaded_calls] { on_backlog_downloaded_calls++; });
  page_sync_->SetOnIdle([this, &on_idle_calls] {
    on_idle_calls++;
    message_loop_.PostQuitTask();
  });
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_backlog_downloaded_calls);
  EXPECT_EQ(1, on_idle_calls);
}

// Verifies that the cloud watcher is registered for the timestamp of the most
// recent commit downloaded from the backlog.
TEST_F(PageSyncImplTest, RegisterWatcher) {
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");
  auth_provider_.token_to_return = "some-token";

  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(std::vector<std::string>{"some-token"},
            cloud_provider_.watch_commits_auth_tokens);
  ASSERT_EQ(1u, cloud_provider_.watch_call_min_timestamps.size());
  EXPECT_EQ("43", cloud_provider_.watch_call_min_timestamps.front());
}

// Verifies that if auth provider fails to provide the auth token, the watcher
// is not set and the error callback is called.
TEST_F(PageSyncImplTest, RegisterWatcherAuthError) {
  auth_provider_.status_to_return = auth_provider::AuthStatus::ERROR;
  auth_provider_.token_to_return = "";
  EXPECT_EQ(0, error_callback_calls_);
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, error_callback_calls_);
  ASSERT_EQ(0u, cloud_provider_.watch_call_min_timestamps.size());
}

// Verifies that commit notifications about new commits in cloud provider are
// received and passed to storage.
TEST_F(PageSyncImplTest, ReceiveNotifications) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));

  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");
  StartPageSync();

  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata[kTimestampKey.ToString()]);
}

// Verify that we retry setting the remote watcher on connection errors
// and when the auth token expires.
TEST_F(PageSyncImplTest, RetryRemoteWatcher) {
  StartPageSync();
  EXPECT_EQ(0u, storage_.received_commits.size());

  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.watch_call_min_timestamps.size() == 1u;
  }));

  page_sync_->OnConnectionError();
  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.watch_call_min_timestamps.size() == 2u;
  }));

  page_sync_->OnTokenExpired();
  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.watch_call_min_timestamps.size() == 3u;
  }));
}

// Verifies that if multiple remote commits are received while one batch is
// already being downloaded, the new remote commits are added to storage in one
// request.
TEST_F(PageSyncImplTest, CoalesceMultipleNotifications) {
  EXPECT_EQ(0u, storage_.received_commits.size());

  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");
  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id3", "content3"), "44");

  // Make the storage delay requests to add remote commits.
  storage_.should_delay_add_commit_confirmation = true;
  StartPageSync();
  bool posted_quit_task = false;
  message_loop_.SetAfterTaskCallback([this, &posted_quit_task] {
    if (posted_quit_task) {
      return;
    }

    if (storage_.delayed_add_commit_confirmations.size() == 1u) {
      message_loop_.PostQuitTask();
      posted_quit_task = true;
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, storage_.delayed_add_commit_confirmations.size());

  // Fire the delayed confirmation.
  storage_.should_delay_add_commit_confirmation = false;
  storage_.delayed_add_commit_confirmations.front()();
  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 3u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  // Verify that all three commits were delivered in total of two calls to
  // storage.
  EXPECT_EQ(3u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("content3", storage_.received_commits["id3"]);
  EXPECT_EQ("44", storage_.sync_metadata[kTimestampKey.ToString()]);
  EXPECT_EQ(2u, storage_.add_commits_from_sync_calls);
}

// Verifies that failing attempts to download the backlog of unsynced commits
// are retried.
TEST_F(PageSyncImplTest, RetryDownloadBacklog) {
  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  StartPageSync();

  // Loop through five attempts to download the backlog.
  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.get_commits_calls == 5u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, storage_.received_commits.size());

  cloud_provider_.status_to_return = cloud_provider_firebase::Status::OK;
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 1u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("42", storage_.sync_metadata[kTimestampKey.ToString()]);
}

// Verifies that a failure to persist the remote commit stops syncing remote
// commits and calls the error callback.
TEST_F(PageSyncImplTest, FailToStoreRemoteCommit) {
  EXPECT_FALSE(cloud_provider_.watcher_removed);
  EXPECT_EQ(0, error_callback_calls_);

  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  storage_.should_fail_add_commit_from_sync = true;
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(cloud_provider_.watcher_removed);
  EXPECT_EQ(1, error_callback_calls_);
}

// Verifies that the on idle callback is called when there is no download in
// progress.
TEST_F(PageSyncImplTest, DownloadIdleCallback) {
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");

  int on_idle_calls = 0;
  page_sync_->SetOnIdle([this, &on_idle_calls] {
    on_idle_calls++;
    message_loop_.PostQuitTask();
  });
  StartPageSync();
  EXPECT_EQ(0, on_idle_calls);
  EXPECT_FALSE(page_sync_->IsIdle());

  // Run the message loop and verify that the sync is idle after all remote
  // commits are added to storage.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_sync_->IsIdle());

  // Notify about a new commit to download and verify that the idle callback was
  // called again on completion.
  std::vector<cloud_provider_firebase::Record> records;
  records.emplace_back(cloud_provider_firebase::Commit("id3", "content3"),
                       "44");
  page_sync_->OnRemoteCommits(std::move(records));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(3u, storage_.received_commits.size());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_sync_->IsIdle());
}

// Verifies that sync correctly fetches objects from the cloud provider.
TEST_F(PageSyncImplTest, GetObject) {
  cloud_provider_.objects_to_return["object_id"] = "content";
  auth_provider_.token_to_return = "some-token";
  StartPageSync();

  storage::Status status;
  uint64_t size;
  zx::socket data;
  page_sync_->GetObject(
      storage::ObjectIdView("object_id"),
      callback::Capture(MakeQuitTask(), &status, &size, &data));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(std::vector<std::string>{"some-token"},
            cloud_provider_.get_object_auth_tokens);
  EXPECT_EQ(7u, size);
  std::string content;
  EXPECT_TRUE(fsl::BlockingCopyToString(std::move(data), &content));
  EXPECT_EQ("content", content);
}

// Verifies that if auth provider fails to provide the auth token, GetObject()
// returns an error, but the sync is not stopped.
TEST_F(PageSyncImplTest, GetObjectAuthError) {
  cloud_provider_.objects_to_return["object_id"] = "content";
  auth_provider_.token_to_return = "some-token";
  page_sync_->SetOnIdle(MakeQuitTask());
  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  auth_provider_.status_to_return = auth_provider::AuthStatus::ERROR;
  auth_provider_.token_to_return = "";
  storage::Status status;
  uint64_t size;
  zx::socket data;
  page_sync_->GetObject(
      storage::ObjectIdView("object_id"),
      callback::Capture(MakeQuitTask(), &status, &size, &data));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(0, error_callback_calls_);
  EXPECT_EQ(storage::Status::IO_ERROR, status);
  EXPECT_EQ(std::vector<std::string>{}, cloud_provider_.get_object_auth_tokens);
  EXPECT_FALSE(data);
}

// Verifies that sync retries GetObject() attempts upon connection error.
TEST_F(PageSyncImplTest, RetryGetObject) {
  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  StartPageSync();

  message_loop_.SetAfterTaskCallback([this] {
    // Allow the operation to succeed after looping through five attempts.
    if (cloud_provider_.get_object_calls == 5u) {
      cloud_provider_.status_to_return = cloud_provider_firebase::Status::OK;
      cloud_provider_.objects_to_return["object_id"] = "content";
    }
  });
  storage::Status status;
  uint64_t size;
  zx::socket data;
  page_sync_->GetObject(
      storage::ObjectIdView("object_id"),
      callback::Capture(MakeQuitTask(), &status, &size, &data));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(6u, cloud_provider_.get_object_calls);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(7u, size);
  std::string content;
  EXPECT_TRUE(fsl::BlockingCopyToString(std::move(data), &content));
  EXPECT_EQ("content", content);
}

// Verifies that uploads are paused until EnableUpload is called.
TEST_F(PageSyncImplTest, UploadIsPaused) {
  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");
  page_sync_->SetOnIdle(MakeQuitTask());

  StartPageSync(UploadStatus::DISABLED);
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(0u, cloud_provider_.received_commits.size());

  page_sync_->EnableUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

  ASSERT_EQ(2u, cloud_provider_.received_commits.size());
}

// Verifies that already synced commit are not re-uploaded.
TEST_F(PageSyncImplTest, DoNotUploadSyncedCommits) {
  page_sync_->SetOnIdle(MakeQuitTask());

  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  auto commit = std::make_unique<TestCommit>("id", "content");
  storage_.new_commits_to_return["id"] = commit->Clone();
  page_sync_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Commit is already synced.
  ASSERT_EQ(0u, cloud_provider_.received_commits.size());
}

// Verifies that commit that are received between the first upload and the retry
// are not sent.
TEST_F(PageSyncImplTest, DoNotUploadSyncedCommitsOnRetry) {
  page_sync_->SetOnIdle(MakeQuitTask());

  StartPageSync();
  EXPECT_FALSE(RunLoopWithTimeout());

  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;

  auto commit = storage_.NewCommit("id", "content");
  storage_.new_commits_to_return["id"] = commit->Clone();
  page_sync_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);

  message_loop_.SetAfterTaskCallback([this] {
    // Stop once cloud provider has rejected a commit.
    if (cloud_provider_.add_commits_calls > 0u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  message_loop_.SetAfterTaskCallback([] {});

  // Commit was rejected.
  ASSERT_EQ(0u, cloud_provider_.received_commits.size());

  cloud_provider_.status_to_return = cloud_provider_firebase::Status::OK;
  cloud_provider_.add_commits_calls = 0u;

  // Simulate the commit being received from the cloud.
  storage_.unsynced_commits_to_return.clear();

  EXPECT_FALSE(RunLoopWithTimeout());

  // Commit is already synced.
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
  EXPECT_EQ(0u, cloud_provider_.add_commits_calls);
}

// Merge commits are deterministic, so can already be in the cloud when we try
// to upload it. The upload will then fail. However, we should stop retrying to
// upload the commit once we received a notification for it through the cloud
// sync watcher.
TEST_F(PageSyncImplTest, UploadCommitAlreadyInCloud) {
  // Complete the initial sync.
  StartPageSync();
  EXPECT_TRUE(
      RunLoopUntil([this] { return cloud_provider_.get_commits_calls > 0u; }));

  // Create a local commit, but make the upload fail.
  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::SERVER_ERROR;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  page_sync_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);

  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.add_commits_calls == 1u &&
           // We need to wait for the callback to be executed on the PageSync
           // side.
           backoff_get_next_calls_ == 1;
  }));

  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_EQ(1, backoff_get_next_calls_);

  // Let's receive the same commit from the remote side.
  std::vector<cloud_provider_firebase::Record> records;
  records.emplace_back(cloud_provider_firebase::Commit("id1", "content1"),
                       "44");
  page_sync_->OnRemoteCommits(std::move(records));

  EXPECT_TRUE(RunLoopUntil([this] { return page_sync_->IsIdle(); }));

  // No additional calls.
  EXPECT_EQ(1u, cloud_provider_.add_commits_calls);
  EXPECT_TRUE(page_sync_->IsIdle());
}

}  // namespace
}  // namespace cloud_sync
