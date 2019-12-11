// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/page_sync_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/gtest/test_loop_fixture.h>

#include <memory>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ledger/bin/cloud_sync/impl/constants.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/backoff/testing/test_backoff.h"
#include "src/ledger/lib/socket/strings.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace cloud_sync {
namespace {

using ::testing::Each;
using testing::ElementsAre;
using ::testing::Truly;

// Creates a dummy continuation token.
cloud_provider::PositionToken MakeToken(convert::ExtendedStringView token_id) {
  cloud_provider::PositionToken token;
  token.opaque_id = convert::ToArray(token_id);
  return token;
}

class TestSyncStateWatcher : public SyncStateWatcher {
 public:
  TestSyncStateWatcher() = default;
  ~TestSyncStateWatcher() override = default;

  void Notify(SyncStateContainer sync_state) override {
    if (!states.empty() && sync_state == *states.rbegin()) {
      return;
    }
    states.push_back(sync_state);
  }

  std::vector<SyncStateContainer> states;
};

class PageSyncImplTest : public ledger::TestWithEnvironment {
 public:
  PageSyncImplTest()
      : storage_(dispatcher()),
        encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()) {
    std::unique_ptr<TestSyncStateWatcher> watcher = std::make_unique<TestSyncStateWatcher>();
    state_watcher_ = watcher.get();

    auto download_backoff = std::make_unique<ledger::TestBackoff>(zx::msec(50));
    download_backoff_ptr_ = download_backoff.get();
    auto upload_backoff = std::make_unique<ledger::TestBackoff>(zx::msec(50));
    upload_backoff_ptr_ = upload_backoff.get();

    page_sync_ = std::make_unique<PageSyncImpl>(
        dispatcher(), environment_.coroutine_service(), &storage_, &storage_, &encryption_service_,
        std::move(page_cloud_ptr_), std::move(download_backoff), std::move(upload_backoff),
        std::move(watcher));
  }
  PageSyncImplTest(const PageSyncImplTest&) = delete;
  PageSyncImplTest& operator=(const PageSyncImplTest&) = delete;
  ~PageSyncImplTest() override = default;

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

  // Adds a commit with a given content to the storage. This method generates an id for the commit
  // based on the content.
  std::unique_ptr<TestCommit> AddLocalCommit(std::string content) {
    storage::CommitId id = storage::ComputeCommitId(content);
    return storage_.NewCommit(id, std::move(content));
  }

  TestPageStorage storage_;
  encryption::FakeEncryptionService encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_ptr_;
  TestPageCloud page_cloud_;
  ledger::TestBackoff* download_backoff_ptr_;
  ledger::TestBackoff* upload_backoff_ptr_;
  TestSyncStateWatcher* state_watcher_;
  std::unique_ptr<PageSyncImpl> page_sync_;
};

SyncStateWatcher::SyncStateContainer MakeStates(DownloadSyncState download,
                                                UploadSyncState upload) {
  return {download, upload};
}

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to PageCloudHandler.
TEST_F(PageSyncImplTest, UploadBacklog) {
  auto id1 = AddLocalCommit("content1")->id;
  auto id2 = AddLocalCommit("content2")->id;
  bool called;
  page_sync_->SetOnPaused(callback::SetWhenCalled(&called));
  StartPageSync();

  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content2");
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count(id1), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count(id2), 1u);

  EXPECT_THAT(state_watcher_->states,
              ElementsAre(MakeStates(DOWNLOAD_BACKLOG, UPLOAD_NOT_STARTED),
                          MakeStates(DOWNLOAD_BACKLOG, UPLOAD_WAIT_REMOTE_DOWNLOAD),
                          MakeStates(DOWNLOAD_SETTING_REMOTE_WATCHER, UPLOAD_WAIT_REMOTE_DOWNLOAD),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_WAIT_REMOTE_DOWNLOAD),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_PENDING),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_IN_PROGRESS),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_IDLE)));
}

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to PageCloudHandler.
TEST_F(PageSyncImplTest, PageWatcher) {
  TestSyncStateWatcher watcher;
  auto id1 = AddLocalCommit("content1")->id;
  auto id2 = AddLocalCommit("content2")->id;
  bool called;
  page_sync_->SetOnPaused(callback::SetWhenCalled(&called));
  page_sync_->SetSyncWatcher(&watcher);
  StartPageSync();

  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_THAT(watcher.states,
              ElementsAre(MakeStates(DOWNLOAD_NOT_STARTED, UPLOAD_NOT_STARTED),
                          MakeStates(DOWNLOAD_BACKLOG, UPLOAD_NOT_STARTED),
                          MakeStates(DOWNLOAD_BACKLOG, UPLOAD_WAIT_REMOTE_DOWNLOAD),
                          MakeStates(DOWNLOAD_SETTING_REMOTE_WATCHER, UPLOAD_WAIT_REMOTE_DOWNLOAD),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_WAIT_REMOTE_DOWNLOAD),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_PENDING),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_IN_PROGRESS),
                          MakeStates(DOWNLOAD_IDLE, UPLOAD_IDLE)));
}

// Verifies that sync pauses uploading commits when it is downloading a commit.
TEST_F(PageSyncImplTest, NoUploadWhenDownloading) {
  storage_.should_delay_add_commit_confirmation = true;

  bool called;
  page_sync_->SetOnPaused(callback::SetWhenCalled(&called));
  StartPageSync();
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_TRUE(page_cloud_.set_watcher.is_bound());
  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content1"});
  ASSERT_TRUE(commit_pack);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("44"), [] {});
  RunLoopUntilIdle();
  EXPECT_LT(0u, storage_.add_commits_from_sync_calls);

  storage_.watcher_->OnNewCommits(AddLocalCommit("content2")->AsList(),
                                  storage::ChangeSource::LOCAL);

  RunLoopUntilIdle();
  EXPECT_FALSE(storage_.delayed_add_commit_confirmations.empty());
  EXPECT_TRUE(page_cloud_.received_commits.empty());

  storage_.delayed_add_commit_confirmations.front()();

  RunLoopUntilIdle();
  EXPECT_FALSE(page_cloud_.received_commits.empty());
}

TEST_F(PageSyncImplTest, UploadExistingCommitsOnlyAfterBacklogDownload) {
  // Verify that two local commits are not uploaded when download is in
  // progress.
  auto id1 = AddLocalCommit("content1")->id;
  auto id2 = AddLocalCommit("content2")->id;

  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content3"));
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content4"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("43"));
  bool backlog_downloaded_called = false;
  page_sync_->SetOnBacklogDownloaded([this, &backlog_downloaded_called] {
    EXPECT_EQ(page_cloud_.received_commits.size(), 0u);
    EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);
    backlog_downloaded_called = true;
  });
  bool called;
  page_sync_->SetOnPaused(callback::SetWhenCalled(&called));
  StartPageSync();

  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_TRUE(backlog_downloaded_called);
  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId(id1)));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId(id2)));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content2");
  ASSERT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count(id1), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count(id2), 1u);
}

// Verifies that existing commits are uploaded before the new ones.
TEST_F(PageSyncImplTest, UploadExistingAndNewCommits) {
  auto id1 = AddLocalCommit("content1")->id;
  storage::CommitId id2;

  page_sync_->SetOnBacklogDownloaded([this, &id2] {
    async::PostTask(dispatcher(), [this, &id2] {
      auto commit = AddLocalCommit("content2");
      id2 = commit->id;
      storage_.watcher_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
    });
  });
  bool called;
  page_sync_->SetOnPaused(callback::SetWhenCalled(&called));

  StartPageSync();
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  ASSERT_FALSE(id2.empty());

  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId(id1)));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId(id2)));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content2");
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count(id1), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count(id2), 1u);
}

// Verifies that the on paused callback is called when there is no pending upload tasks.
TEST_F(PageSyncImplTest, UploadPausedCallback) {
  int on_paused_calls = 0;

  auto id1 = AddLocalCommit("content1")->id;
  auto id2 = AddLocalCommit("content2")->id;

  page_sync_->SetOnPaused([&on_paused_calls] { on_paused_calls++; });
  StartPageSync();

  // Verify that the paused callback is called once both commits are uploaded.
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.received_commits.size(), 2u);
  EXPECT_EQ(on_paused_calls, 1);
  EXPECT_TRUE(page_sync_->IsPaused());

  // Notify about a new commit to upload and verify that the paused callback was called again on
  // completion.
  auto commit3 = AddLocalCommit("content3");
  storage_.watcher_->OnNewCommits(commit3->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_sync_->IsPaused());
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.received_commits.size(), 3u);
  EXPECT_EQ(on_paused_calls, 2);
  EXPECT_TRUE(page_sync_->IsPaused());
}

TEST_F(PageSyncImplTest, PausedOnUploadTemporaryError) {
  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  int on_paused_calls = 0;

  auto id1 = AddLocalCommit("content1")->id;
  auto id2 = AddLocalCommit("content2")->id;

  page_sync_->SetOnPaused([&on_paused_calls] { on_paused_calls++; });
  StartPageSync();

  // Verify that the paused callback is called.
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.add_commits_calls, 1u);
  EXPECT_EQ(on_paused_calls, 1);
  EXPECT_TRUE(page_sync_->IsPaused());
  EXPECT_EQ(state_watcher_->states.back().upload, UPLOAD_TEMPORARY_ERROR);
}

// Verifies that a failure to persist the remote commit stops syncing remote commits and calls the
// error callback.
TEST_F(PageSyncImplTest, FailToStoreRemoteCommit) {
  bool on_paused_called;
  page_sync_->SetOnPaused(callback::SetWhenCalled(&on_paused_called));
  int error_callback_calls = 0;
  page_sync_->SetOnUnrecoverableError([&error_callback_calls] { error_callback_calls++; });
  StartPageSync();
  RunLoopUntilIdle();
  ASSERT_TRUE(on_paused_called);
  ASSERT_TRUE(page_cloud_.set_watcher.is_bound());

  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content1"});
  ASSERT_TRUE(commit_pack);
  storage_.should_fail_add_commit_from_sync = true;
  EXPECT_EQ(error_callback_calls, 0);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("42"), [] {});

  on_paused_called = false;
  RunLoopUntilIdle();
  EXPECT_TRUE(on_paused_called);
  EXPECT_FALSE(page_cloud_.set_watcher.is_bound());
  EXPECT_EQ(error_callback_calls, 1);
}

// Verifies that the on paused callback is called when there is no download in progress.
TEST_F(PageSyncImplTest, DownloadIdleCallback) {
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content1"));
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content2"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("43"));

  int on_paused_calls = 0;
  page_sync_->SetOnPaused([&on_paused_calls] { on_paused_calls++; });
  StartPageSync();
  EXPECT_EQ(on_paused_calls, 0);
  EXPECT_FALSE(page_sync_->IsPaused());

  // Run the message loop and verify that the sync is paused after all remote commits are added to
  // storage.
  RunLoopUntilIdle();
  EXPECT_EQ(on_paused_calls, 1);
  EXPECT_TRUE(page_sync_->IsPaused());
  EXPECT_EQ(storage_.received_commits.size(), 2u);

  // Notify about a new commit to download and verify that the paused callback was called again on
  // completion.
  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content3"});
  ASSERT_TRUE(commit_pack);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("44"), [] {});
  RunLoopUntilIdle();
  EXPECT_EQ(storage_.received_commits.size(), 3u);
  EXPECT_EQ(on_paused_calls, 2);
  EXPECT_TRUE(page_sync_->IsPaused());
}

TEST_F(PageSyncImplTest, PausedOnDownloadTemporaryError) {
  page_cloud_.status_to_return = cloud_provider::Status::NETWORK_ERROR;
  int on_paused_calls = 0;

  page_sync_->SetOnPaused([&on_paused_calls] { on_paused_calls++; });
  StartPageSync();

  // Verify that the paused callback is called.
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.get_commits_calls, 1u);
  EXPECT_EQ(on_paused_calls, 1);
  EXPECT_TRUE(page_sync_->IsPaused());
  EXPECT_EQ(state_watcher_->states.back().download, DOWNLOAD_TEMPORARY_ERROR);
  EXPECT_EQ(state_watcher_->states.back().upload, UPLOAD_WAIT_REMOTE_DOWNLOAD);
}

// Verifies that uploads are paused until EnableUpload is called.
TEST_F(PageSyncImplTest, UploadIsPaused) {
  AddLocalCommit("content1");
  AddLocalCommit("content2");
  bool called;
  page_sync_->SetOnPaused(callback::SetWhenCalled(&called));

  StartPageSync(UploadStatus::DISABLED);
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  ASSERT_EQ(page_cloud_.received_commits.size(), 0u);

  page_sync_->EnableUpload();
  RunLoopUntilIdle();

  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
}

// Merge commits are deterministic, so can already be in the cloud when we try
// to upload it. The upload will then fail. However, we should stop retrying to
// upload the commit once we received a notification for it through the cloud
// sync watcher.
TEST_F(PageSyncImplTest, UploadCommitAlreadyInCloud) {
  // Complete the initial sync.
  StartPageSync();
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.get_commits_calls, 1u);

  // Create a local commit, but make the upload fail.
  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  auto commit1 = AddLocalCommit("content1");
  storage_.watcher_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);

  // We need to wait for the callback to be executed on the PageSync side.
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.add_commits_calls, 1u);
  EXPECT_EQ(upload_backoff_ptr_->get_next_count, 1);

  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());

  // Let's receive the same commit from the remote side.
  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content1"});
  ASSERT_TRUE(commit_pack);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("44"), [] {});
  RunLoopUntilIdle();
  EXPECT_TRUE(page_sync_->IsPaused());

  // No additional calls.
  EXPECT_EQ(page_cloud_.add_commits_calls, 1u);
  EXPECT_TRUE(page_sync_->IsPaused());
}

TEST_F(PageSyncImplTest, UnrecoverableError) {
  int on_error_calls = 0;
  page_sync_->SetOnUnrecoverableError([&on_error_calls] { on_error_calls++; });
  // Complete the initial sync.
  StartPageSync();
  RunLoopUntilIdle();
  EXPECT_EQ(on_error_calls, 0);

  page_cloud_.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(on_error_calls, 1);
}

// Test that when both OnPaused and OnUnrecoverableError attempt to delete PageSync, only one is
// triggered.
TEST_F(PageSyncImplTest, AvoidDoubleDeleteOnDownloadError) {
  bool ready_to_delete = false;
  auto delete_callback = [this, &ready_to_delete] {
    ASSERT_NE(page_sync_, nullptr);
    if (ready_to_delete) {
      page_sync_.reset();
    }
  };
  page_sync_->SetOnPaused(delete_callback);
  page_sync_->SetOnUnrecoverableError(delete_callback);
  StartPageSync();
  RunLoopUntilIdle();

  ready_to_delete = true;
  page_sync_->SetDownloadState(DownloadSyncState::DOWNLOAD_PERMANENT_ERROR);
  ASSERT_EQ(page_sync_, nullptr);
}

TEST_F(PageSyncImplTest, AvoidDoubleDeleteOnUploadError) {
  bool ready_to_delete = false;
  auto delete_callback = [this, &ready_to_delete] {
    ASSERT_NE(page_sync_, nullptr);
    if (ready_to_delete) {
      page_sync_.reset();
    }
  };
  page_sync_->SetOnPaused(delete_callback);
  page_sync_->SetOnUnrecoverableError(delete_callback);
  StartPageSync();
  RunLoopUntilIdle();

  ready_to_delete = true;
  page_sync_->SetUploadState(UploadSyncState::UPLOAD_PERMANENT_ERROR);
  ASSERT_EQ(page_sync_, nullptr);
}

}  // namespace
}  // namespace cloud_sync
