// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/page_upload.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <memory>
#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "src/ledger/bin/cloud_sync/impl/clock_pack.h"
#include "src/ledger/bin/cloud_sync/impl/constants.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/encoding/encoding.h"
#include "src/ledger/lib/socket/strings.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/backoff/testing/test_backoff.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace cloud_sync {
namespace {

using ::testing::Each;
using ::testing::SizeIs;
using ::testing::Truly;

constexpr zx::duration kBackoffInterval = zx::msec(10);
constexpr zx::duration kHalfBackoffInterval = zx::msec(5);

// Creates a dummy continuation token.
cloud_provider::PositionToken MakeToken(convert::ExtendedStringView token_id) {
  cloud_provider::PositionToken token;
  token.opaque_id = convert::ToArray(token_id);
  return token;
}

class PageUploadTest : public ledger::TestWithEnvironment, public PageUpload::Delegate {
 public:
  PageUploadTest()
      : storage_(dispatcher()),
        encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()),
        task_runner_(dispatcher()) {
    auto test_backoff = std::make_unique<backoff::TestBackoff>(kBackoffInterval);
    backoff_ = test_backoff.get();
    page_upload_ = std::make_unique<PageUpload>(environment_.coroutine_service(), &task_runner_,
                                                &storage_, &encryption_service_, &page_cloud_ptr_,
                                                this, std::move(test_backoff));
  }
  PageUploadTest(const PageUploadTest&) = delete;
  PageUploadTest& operator=(const PageUploadTest&) = delete;
  ~PageUploadTest() override = default;

 protected:
  void SetOnNewStateCallback(fit::closure callback) { new_state_callback_ = std::move(callback); }

  void SetUploadState(UploadSyncState sync_state) override {
    states_.push_back(sync_state);
    if (new_state_callback_) {
      new_state_callback_();
    }
  }

  bool IsDownloadIdle() override { return is_download_idle_; }

  TestPageStorage storage_;
  encryption::FakeEncryptionService encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_ptr_;
  TestPageCloud page_cloud_;
  std::vector<UploadSyncState> states_;
  std::unique_ptr<PageUpload> page_upload_;
  backoff::TestBackoff* backoff_;
  bool is_download_idle_ = true;

 private:
  fit::closure new_state_callback_;
  callback::ScopedTaskRunner task_runner_;
};

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to PageCloudHandler.
TEST_F(PageUploadTest, UploadBacklog) {
  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();

  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);

  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id2")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content2");
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id2"), 1u);
}

// Verifies that the backlog of commits to upload is not uploaded until there's
// only one local head.
TEST_F(PageUploadTest, UploadBacklogOnlyOnSingleHead) {
  // Verify that two local commits are not uploaded when there is two local
  // heads.
  bool upload_is_paused = false;
  storage_.head_count = 2;
  storage_.NewCommit("id0", "content0");
  storage_.NewCommit("id1", "content1");
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();

  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);

  // Add a new commit and reduce the number of heads to 1.
  upload_is_paused = false;
  storage_.head_count = 1;
  auto commit = storage_.NewCommit("id2", "content2");
  storage_.watcher_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);

  // Verify that all local commits were uploaded.
  ASSERT_EQ(page_cloud_.received_commits.size(), 3u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id0")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content0");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[2].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id2")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[2].data()),
            "content2");
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 3u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id0"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id2"), 1u);
}

TEST_F(PageUploadTest, UploadExistingCommitsOnlyAfterBacklogDownload) {
  // Verify that two local commits are not uploaded when download is in
  // progress.
  storage_.NewCommit("local1", "content1");
  storage_.NewCommit("local2", "content2");

  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content3"));
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content4"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("44"));

  is_download_idle_ = false;
  bool upload_wait_remote_download = false;
  SetOnNewStateCallback([this, &upload_wait_remote_download] {
    if (states_.back() == UPLOAD_WAIT_REMOTE_DOWNLOAD) {
      upload_wait_remote_download = true;
    }
  });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_wait_remote_download);

  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);

  is_download_idle_ = true;
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);

  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("local1")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("local2")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content2");
  ASSERT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("local1"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("local2"), 1u);
}

// Verfies that the new commits that PageSync is notified about through storage
// watcher are uploaded to PageCloudHandler, with the exception of commits that
// themselves come from sync.
TEST_F(PageUploadTest, UploadNewCommits) {
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;

  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.watcher_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);

  // The commit coming from sync should be ignored.
  auto commit2 = storage_.NewCommit("id2", "content2", false);
  storage_.watcher_->OnNewCommits(commit2->AsList(), storage::ChangeSource::CLOUD);

  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.watcher_->OnNewCommits(commit3->AsList(), storage::ChangeSource::LOCAL);

  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);

  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id3")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content3");
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id3"), 1u);
}

// Verifies that new commits being added to storage are only uploaded while
// there is only a single head.
TEST_F(PageUploadTest, UploadNewCommitsOnlyOnSingleHead) {
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;

  // Add a new commit when there's only one head and verify that it is
  // uploaded.
  storage_.head_count = 1;
  auto commit0 = storage_.NewCommit("id0", "content0");
  storage_.watcher_->OnNewCommits(commit0->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsPaused());
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;
  ASSERT_EQ(page_cloud_.received_commits.size(), 1u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id0")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content0");
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id0"), 1u);

  // Add another commit when there's two heads and verify that it is not
  // uploaded.
  page_cloud_.received_commits.clear();
  storage_.head_count = 2;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.watcher_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;
  ASSERT_EQ(page_cloud_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 0u);

  // Add another commit bringing the number of heads down to one and verify that
  // both commits are uploaded.
  storage_.head_count = 1;
  auto commit2 = storage_.NewCommit("id2", "content2");
  storage_.watcher_->OnNewCommits(commit2->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsPaused());
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id2")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content2");
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id2"), 1u);
}

// Verifies that existing commits are uploaded before the new ones.
TEST_F(PageUploadTest, UploadExistingAndNewCommits) {
  storage_.NewCommit("id1", "content1");
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;

  auto commit = storage_.NewCommit("id2", "content2");
  storage_.watcher_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);

  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content1");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id2")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content2");
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id2"), 1u);
}

// Verifies that failing uploads are retried.
TEST_F(PageUploadTest, RetryUpload) {
  page_upload_->StartOrRestartUpload();
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  SetOnNewStateCallback(nullptr);

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.watcher_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);
  RunLoopFor(kHalfBackoffInterval);
  EXPECT_GE(backoff_->get_next_count, 0);
  RunLoopFor(kBackoffInterval);
  EXPECT_GE(backoff_->get_next_count, 1);
  RunLoopFor(kBackoffInterval);
  EXPECT_GE(backoff_->get_next_count, 2);

  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
}

// Verifies that the idle status is returned when there is no pending upload
// task.
TEST_F(PageUploadTest, UploadIdleStatus) {
  int on_idle_calls = 0;

  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");

  SetOnNewStateCallback([this, &on_idle_calls] {
    if (states_.back() == UPLOAD_IDLE) {
      on_idle_calls++;
    }
  });
  page_upload_->StartOrRestartUpload();

  // Verify that the idle callback is called once both commits are uploaded.
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.received_commits.size(), 2u);
  EXPECT_EQ(on_idle_calls, 1);
  EXPECT_TRUE(page_upload_->IsPaused());

  // Notify about a new commit to upload and verify that the idle callback was
  // called again on completion.
  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.watcher_->OnNewCommits(commit3->AsList(), storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsPaused());
  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.received_commits.size(), 3u);
  EXPECT_EQ(on_idle_calls, 2);
  EXPECT_TRUE(page_upload_->IsPaused());
}

// Verifies that if listing the original commits to be uploaded fails, the
// client is notified about the error.
TEST_F(PageUploadTest, FailToListCommits) {
  EXPECT_FALSE(storage_.watcher_set);
  int error_calls = 0;
  storage_.should_fail_get_unsynced_commits = true;
  SetOnNewStateCallback([this, &error_calls] {
    if (states_.back() == UPLOAD_PERMANENT_ERROR) {
      error_calls++;
    }
  });

  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  EXPECT_EQ(error_calls, 1);
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);
}

// Verifies that already synced commits are not re-uploaded.
TEST_F(PageUploadTest, DoNotUploadSyncedCommits) {
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;

  auto commit = std::make_unique<TestCommit>("id", "content");
  storage_.watcher_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);

  // Commit is already synced.
  ASSERT_EQ(page_cloud_.received_commits.size(), 0u);
}

// Verifies that commits that are received between the first upload and the
// retry are not sent.
TEST_F(PageUploadTest, DoNotUploadSyncedCommitsOnRetry) {
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] {
    upload_is_paused = page_upload_->IsPaused();
    if (states_.back() == UploadSyncState::UPLOAD_TEMPORARY_ERROR) {
      QuitLoop();
    }
  });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;

  auto commit = storage_.NewCommit("id", "content");
  storage_.watcher_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);

  // The page upload should run into temporary error.
  RunLoopUntilIdle();
  EXPECT_EQ(states_.back(), UploadSyncState::UPLOAD_TEMPORARY_ERROR);
  EXPECT_GT(page_cloud_.add_commits_calls, 0u);

  // Configure the cloud to accept the next attempt to upload.
  page_cloud_.commit_status_to_return = cloud_provider::Status::OK;
  page_cloud_.add_commits_calls = 0u;

  // Make storage report the commit as synced (not include it in the list of
  // unsynced commits to return).
  storage_.unsynced_commits_to_return.clear();

  RunLoopFor(kHalfBackoffInterval);
  EXPECT_EQ(states_.back(), UploadSyncState::UPLOAD_TEMPORARY_ERROR);
  EXPECT_TRUE(upload_is_paused);
  RunLoopFor(kBackoffInterval);
  EXPECT_EQ(states_.back(), UploadSyncState::UPLOAD_IDLE);
  EXPECT_TRUE(upload_is_paused);

  // Verify that no calls were made to attempt to upload the commit.
  EXPECT_EQ(page_cloud_.add_commits_calls, 0u);
}

// Verifies that concurrent new commit notifications do not crash PageUpload.
TEST_F(PageUploadTest, UploadNewCommitsConcurrentNoCrash) {
  bool upload_is_paused = false;
  SetOnNewStateCallback([this, &upload_is_paused] { upload_is_paused = page_upload_->IsPaused(); });
  page_upload_->StartOrRestartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_paused);
  upload_is_paused = false;

  storage_.head_count = 2;
  auto commit0 = storage_.NewCommit("id0", "content0");
  storage_.watcher_->OnNewCommits(commit0->AsList(), storage::ChangeSource::LOCAL);

  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.watcher_->OnNewCommits(commit1->AsList(), storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
}

// Verifies that if upload fails when sending the object, we don't create a new batch and call
// GetUnsyncedCommits again.
TEST_F(PageUploadTest, RetryOnError) {
  storage_.NewCommit("id", "content");
  auto id = encryption_service_.MakeObjectIdentifier(storage_.GetObjectIdentifierFactory(),
                                                     storage::ObjectDigest("obj_digest"));
  storage_.unsynced_objects_to_return[id] =
      std::make_unique<storage::fake::FakePiece>(id, "obj_data");

  int failed_upload_count = 30;
  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  // Fail the upload |failed_upload_count| times.
  SetOnNewStateCallback([this, &failed_upload_count] {
    if (states_.back() == UploadSyncState::UPLOAD_TEMPORARY_ERROR) {
      ASSERT_GE(failed_upload_count, 0);
      failed_upload_count--;
      if (failed_upload_count == 0) {
        page_cloud_.object_status_to_return = cloud_provider::Status::OK;
      }
    }
  });
  page_upload_->StartOrRestartUpload();
  RunLoopFor(kBackoffInterval * 30);

  EXPECT_EQ(failed_upload_count, 0);
  // GetUnsyncedCommits should be called twice: once when creating the batch and once just before
  // uploading the commits.
  EXPECT_EQ(storage_.get_unsynced_commits_calls, 2u);
  EXPECT_EQ(page_cloud_.add_commits_calls, 1u);
  EXPECT_EQ(states_.back(), UploadSyncState::UPLOAD_IDLE);
}

// Verifies that clocks are uploaded.
TEST_F(PageUploadTest, UploadClock) {
  page_upload_->StartOrRestartUpload();
  storage::Clock clock{
      {clocks::DeviceId{"device_0", 1}, storage::ClockTombstone{}},
      {clocks::DeviceId{"device_1", 1},
       storage::DeviceEntry{storage::ClockEntry{"commit1", 1}, storage::ClockEntry{"commit0", 0}}},
      {clocks::DeviceId{"device_2", 4},
       storage::DeviceEntry{storage::ClockEntry{"commit4", 4}, storage::ClockEntry{"commit2", 2}}}};
  bool called;
  ledger::Status status;
  page_upload_->UpdateClock(std::move(clock),
                            callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_THAT(page_cloud_.clocks, SizeIs(1));

  page_cloud_.clocks[0].second(
      cloud_provider::Status::OK,
      std::make_unique<cloud_provider::ClockPack>(std::move(page_cloud_.clocks[0].first)));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
}

// Verifies that clocks uploads are buffered while waiting for the cloud response.
TEST_F(PageUploadTest, UploadClockRateLimit) {
  storage_.remote_id_to_commit_id[encryption_service_.EncodeCommitId("commit0")] = "commit0";
  storage_.remote_id_to_commit_id[encryption_service_.EncodeCommitId("commit1")] = "commit1";
  storage_.remote_id_to_commit_id[encryption_service_.EncodeCommitId("commit5")] = "commit5";
  page_upload_->StartOrRestartUpload();
  storage::Clock clock{
      {clocks::DeviceId{"device_1", 1},
       storage::DeviceEntry{storage::ClockEntry{"commit1", 1}, storage::ClockEntry{"commit0", 0}}}};
  bool called_1;
  ledger::Status status_1;
  page_upload_->UpdateClock(clock,
                            callback::Capture(callback::SetWhenCalled(&called_1), &status_1));
  RunLoopUntilIdle();
  EXPECT_FALSE(called_1);
  EXPECT_THAT(page_cloud_.clocks, SizeIs(1));

  bool called_2;
  ledger::Status status_2;
  page_upload_->UpdateClock(clock,
                            callback::Capture(callback::SetWhenCalled(&called_2), &status_2));
  // The second call is waiting for the first one to finish.
  RunLoopUntilIdle();
  EXPECT_FALSE(called_2);
  EXPECT_THAT(page_cloud_.clocks, SizeIs(1));

  storage::Clock clock_3{
      {clocks::DeviceId{"device_1", 1},
       storage::DeviceEntry{storage::ClockEntry{"commit5", 5}, storage::ClockEntry{"commit0", 0}}}};
  bool called_3;
  ledger::Status status_3;
  page_upload_->UpdateClock(clock_3,
                            callback::Capture(callback::SetWhenCalled(&called_3), &status_3));
  // The second call is waiting for the first one to finish.
  RunLoopUntilIdle();
  EXPECT_FALSE(called_3);
  EXPECT_THAT(page_cloud_.clocks, SizeIs(1));

  // Respond to the first request. The second request should be sent, with the third clock.
  page_cloud_.clocks[0].second(
      cloud_provider::Status::OK,
      std::make_unique<cloud_provider::ClockPack>(std::move(page_cloud_.clocks[0].first)));

  RunLoopUntilIdle();
  EXPECT_TRUE(called_1);
  EXPECT_EQ(status_1, ledger::Status::OK);
  EXPECT_THAT(page_cloud_.clocks, SizeIs(2));

  auto golden_clock = clock_3;
  // Remove the cloud entry from the clocks: it is not transmitted either way.
  std::get<storage::DeviceEntry>(golden_clock.begin()->second).cloud.reset();
  storage::Clock actual_clock;
  ledger::Status status;
  EXPECT_TRUE(RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    status = DecodeClock(handler, &storage_, std::move(page_cloud_.clocks[1].first), &actual_clock);
  }));
  EXPECT_EQ(status, ledger::Status::OK);
  std::get<storage::DeviceEntry>(actual_clock.begin()->second).cloud.reset();
  EXPECT_EQ(actual_clock, golden_clock);

  auto return_clock_pack = EncodeClock(&encryption_service_, actual_clock);
  page_cloud_.clocks[1].second(
      cloud_provider::Status::OK,
      std::make_unique<cloud_provider::ClockPack>(std::move(return_clock_pack)));

  RunLoopUntilIdle();
  EXPECT_TRUE(called_2);
  EXPECT_EQ(status_2, ledger::Status::OK);
  EXPECT_TRUE(called_3);
  EXPECT_EQ(status_3, ledger::Status::OK);
}

// Verifies that if we receive new commits during backoff:
//  - the retried upload only uploads commits that existed when it was started.
//  - a second upload starts immediately after the first (retried) upload succeeds and uploads the
//    new commits.
TEST_F(PageUploadTest, NewCommitsDuringBackoff) {
  storage_.NewCommit("id", "content");
  auto id = encryption_service_.MakeObjectIdentifier(storage_.GetObjectIdentifierFactory(),
                                                     storage::ObjectDigest("obj_digest"));
  storage_.unsynced_objects_to_return[id] =
      std::make_unique<storage::fake::FakePiece>(id, "obj_data");

  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  // Wait for the upload to fail: the upload state becomes UPLOAD_TEMPORARY_ERROR.  While the upload
  // is in backoff, we ensure that the retry will succeed, we create a new local commit and notify
  // the page upload object.
  SetOnNewStateCallback([this] {
    if (states_.back() == UploadSyncState::UPLOAD_TEMPORARY_ERROR) {
      page_cloud_.object_status_to_return = cloud_provider::Status::OK;
      auto commit = storage_.NewCommit("id2", "content2");
      storage_.watcher_->OnNewCommits(commit->AsList(), storage::ChangeSource::LOCAL);
    }
  });
  page_upload_->StartOrRestartUpload();
  RunLoopFor(kBackoffInterval);

  // Two uploads have been done.
  EXPECT_EQ(page_cloud_.add_commits_calls, 2u);
  EXPECT_EQ(states_.back(), UploadSyncState::UPLOAD_IDLE);
  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
}

}  // namespace
}  // namespace cloud_sync
