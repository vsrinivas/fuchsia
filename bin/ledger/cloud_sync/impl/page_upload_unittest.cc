// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_upload.h"

#include <memory>
#include <utility>
#include <vector>

#include "lib/backoff/backoff.h"
#include "lib/backoff/testing/test_backoff.h"
#include "lib/callback/capture.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"
#include "peridot/bin/ledger/cloud_sync/impl/testing/test_page_cloud.h"
#include "peridot/bin/ledger/cloud_sync/impl/testing/test_page_storage.h"
#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/testing/commit_empty_impl.h"
#include "peridot/bin/ledger/storage/testing/page_storage_empty_impl.h"

namespace cloud_sync {
namespace {

// Creates a dummy continuation token.
std::unique_ptr<cloud_provider::Token> MakeToken(
    convert::ExtendedStringView token_id) {
  auto token = std::make_unique<cloud_provider::Token>();
  token->opaque_id = convert::ToArray(token_id);
  return token;
}

class PageUploadTest : public gtest::TestLoopFixture, public PageUpload::Delegate {
 public:
  PageUploadTest()
      : storage_(dispatcher()),
        encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()),
        task_runner_(dispatcher()) {
    auto test_backoff = std::make_unique<backoff::TestBackoff>();
    backoff_ = test_backoff.get();
    page_upload_ = std::make_unique<PageUpload>(
        &task_runner_, &storage_, &encryption_service_, &page_cloud_ptr_, this,
        std::move(test_backoff));
  }
  ~PageUploadTest() override {}

 protected:
  void SetOnNewStateCallback(fxl::Closure callback) {
    new_state_callback_ = std::move(callback);
  }

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
  fxl::Closure new_state_callback_;
  callback::ScopedTaskRunner task_runner_;
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUploadTest);
};

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to PageCloudHandler.
TEST_F(PageUploadTest, UploadBacklog) {
  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();

  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);

  ASSERT_EQ(2u, page_cloud_.received_commits.size());
  EXPECT_EQ("id1", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content1", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ("id2", page_cloud_.received_commits[1].id);
  EXPECT_EQ("content2", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[1].data));
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

// Verifies that the backlog of commits to upload is not uploaded until there's
// only one local head.
TEST_F(PageUploadTest, UploadBacklogOnlyOnSingleHead) {
  // Verify that two local commits are not uploaded when there is two local
  // heads.
  bool upload_is_idle = false;
  storage_.head_count = 2;
  storage_.NewCommit("id0", "content0");
  storage_.NewCommit("id1", "content1");
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();

  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  EXPECT_EQ(0u, page_cloud_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());

  // Add a new commit and reduce the number of heads to 1.
  upload_is_idle = false;
  storage_.head_count = 1;
  auto commit = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);

  // Verify that all local commits were uploaded.
  ASSERT_EQ(3u, page_cloud_.received_commits.size());
  EXPECT_EQ("id0", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content0", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ("id1", page_cloud_.received_commits[1].id);
  EXPECT_EQ("content1", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[1].data));
  EXPECT_EQ("id2", page_cloud_.received_commits[2].id);
  EXPECT_EQ("content2", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[2].data));
  EXPECT_EQ(3u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id0"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

TEST_F(PageUploadTest, UploadExistingCommitsOnlyAfterBacklogDownload) {
  // Verify that two local commits are not uploaded when download is in
  // progress.
  storage_.NewCommit("local1", "content1");
  storage_.NewCommit("local2", "content2");

  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "remote3", "content3"));
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "remote4", "content4"));
  page_cloud_.position_token_to_return = MakeToken("44");

  is_download_idle_ = false;
  bool upload_wait_remote_download = false;
  SetOnNewStateCallback([this, &upload_wait_remote_download] {
    if (states_.back() == UPLOAD_WAIT_REMOTE_DOWNLOAD) {
      upload_wait_remote_download = true;
    }
  });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_wait_remote_download);

  EXPECT_EQ(0u, page_cloud_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());

  is_download_idle_ = true;
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);

  ASSERT_EQ(2u, page_cloud_.received_commits.size());
  EXPECT_EQ("local1", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content1", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ("local2", page_cloud_.received_commits[1].id);
  EXPECT_EQ("content2", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[1].data));
  ASSERT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("local1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("local2"));
}

// Verfies that the new commits that PageSync is notified about through storage
// watcher are uploaded to PageCloudHandler, with the exception of commits that
// themselves come from sync.
TEST_F(PageUploadTest, UploadNewCommits) {
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;

  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  storage_.watcher_->OnNewCommits(commit1->AsList(),
                                  storage::ChangeSource::LOCAL);

  // The commit coming from sync should be ignored.
  auto commit2 = storage_.NewCommit("id2", "content2", false);
  storage_.new_commits_to_return["id2"] = commit2->Clone();
  storage_.watcher_->OnNewCommits(commit2->AsList(),
                                  storage::ChangeSource::CLOUD);

  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.new_commits_to_return["id3"] = commit3->Clone();
  storage_.watcher_->OnNewCommits(commit3->AsList(),
                                  storage::ChangeSource::LOCAL);

  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);

  ASSERT_EQ(2u, page_cloud_.received_commits.size());
  EXPECT_EQ("id1", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content1", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ("id3", page_cloud_.received_commits[1].id);
  EXPECT_EQ("content3", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[1].data));
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id3"));
}

// Verifies that new commits being added to storage are only uploaded while
// there is only a single head.
TEST_F(PageUploadTest, UploadNewCommitsOnlyOnSingleHead) {
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;

  // Add a new commit when there's only one head and verify that it is
  // uploaded.
  storage_.head_count = 1;
  auto commit0 = storage_.NewCommit("id0", "content0");
  storage_.new_commits_to_return["id0"] = commit0->Clone();
  storage_.watcher_->OnNewCommits(commit0->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsIdle());
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;
  ASSERT_EQ(1u, page_cloud_.received_commits.size());
  EXPECT_EQ("id0", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content0", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id0"));

  // Add another commit when there's two heads and verify that it is not
  // uploaded.
  page_cloud_.received_commits.clear();
  storage_.head_count = 2;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  storage_.watcher_->OnNewCommits(commit1->AsList(),
                                  storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;
  ASSERT_EQ(0u, page_cloud_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.count("id1"));

  // Add another commit bringing the number of heads down to one and verify that
  // both commits are uploaded.
  storage_.head_count = 1;
  auto commit2 = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit2->Clone();
  storage_.watcher_->OnNewCommits(commit2->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsIdle());
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  ASSERT_EQ(2u, page_cloud_.received_commits.size());
  EXPECT_EQ("id1", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content1", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ("id2", page_cloud_.received_commits[1].id);
  EXPECT_EQ("content2", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[1].data));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

// Verifies that existing commits are uploaded before the new ones.
TEST_F(PageUploadTest, UploadExistingAndNewCommits) {
  storage_.NewCommit("id1", "content1");
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;

  auto commit = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);

  ASSERT_EQ(2u, page_cloud_.received_commits.size());
  EXPECT_EQ("id1", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content1", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ("id2", page_cloud_.received_commits[1].id);
  EXPECT_EQ("content2", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[1].data));
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

// Verifies that failing uploads are retried. In production the retries are
// delayed, here we set the delays to 0.
TEST_F(PageUploadTest, DISABLED_RetryUpload) {
  page_upload_->StartUpload();
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  SetOnNewStateCallback(nullptr);

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  storage_.watcher_->OnNewCommits(commit1->AsList(),
                                  storage::ChangeSource::LOCAL);

  // TODO(LE-494): update this case and the following logic to be more
  // TestLoop-friendly.
#if 0
  // Test cloud provider logs every commit, even if it reports that upload
  // failed for each. Here we loop through at least five attempts to upload the
  // commit.
  EXPECT_TRUE(RunLoopUntil([this] {
    return page_cloud_.add_commits_calls >= 5u &&
           // We need to wait for the callback to be executed on the PageSync
           // side.
           backoff_->get_next_count >= 5;
  }));
#endif
  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_GE(backoff_->get_next_count, 5);
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
  page_upload_->StartUpload();

  // Verify that the idle callback is called once both commits are uploaded.
  RunLoopUntilIdle();
  EXPECT_EQ(2u, page_cloud_.received_commits.size());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_upload_->IsIdle());

  // Notify about a new commit to upload and verify that the idle callback was
  // called again on completion.
  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.new_commits_to_return["id3"] = commit3->Clone();
  storage_.watcher_->OnNewCommits(commit3->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsIdle());
  RunLoopUntilIdle();
  EXPECT_EQ(3u, page_cloud_.received_commits.size());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_upload_->IsIdle());
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

  page_upload_->StartUpload();
  RunLoopUntilIdle();
  EXPECT_EQ(1, error_calls);
  EXPECT_EQ(0u, page_cloud_.received_commits.size());
}

// Verifies that already synced commits are not re-uploaded.
TEST_F(PageUploadTest, DoNotUploadSyncedCommits) {
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;

  auto commit = std::make_unique<TestCommit>("id", "content");
  storage_.new_commits_to_return["id"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);

  // Commit is already synced.
  ASSERT_EQ(0u, page_cloud_.received_commits.size());
}

// Verifies that commits that are received between the first upload and the
// retry are not sent.
TEST_F(PageUploadTest, DoNotUploadSyncedCommitsOnRetry) {
  bool upload_is_idle = false;
  SetOnNewStateCallback([this, &upload_is_idle] {
    upload_is_idle = page_upload_->IsIdle();
    if (states_.back() == UploadSyncState::UPLOAD_TEMPORARY_ERROR) {
      QuitLoop();
    }
  });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;

  auto commit = storage_.NewCommit("id", "content");
  storage_.new_commits_to_return["id"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);

  // The page upload should run into temporary error.
  RunLoopUntilIdle();
  EXPECT_EQ(UploadSyncState::UPLOAD_TEMPORARY_ERROR, states_.back());
  EXPECT_GT(page_cloud_.add_commits_calls, 0u);

  // Configure the cloud to accept the next attempt to upload.
  page_cloud_.commit_status_to_return = cloud_provider::Status::OK;
  page_cloud_.add_commits_calls = 0u;

  // Make storage report the commit as synced (not include it in the list of
  // unsynced commits to return).
  storage_.unsynced_commits_to_return.clear();

  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);

  // Verify that no calls were made to attempt to upload the commit.
  EXPECT_EQ(0u, page_cloud_.add_commits_calls);
}

// Verifies that concurrent new commit notifications do not crash PageUpload.
TEST_F(PageUploadTest, UploadNewCommitsConcurrentNoCrash) {
  bool upload_is_idle = false;
  SetOnNewStateCallback(
      [this, &upload_is_idle] { upload_is_idle = page_upload_->IsIdle(); });
  page_upload_->StartUpload();
  RunLoopUntilIdle();
  ASSERT_TRUE(upload_is_idle);
  upload_is_idle = false;

  storage_.head_count = 2;
  storage_.should_delay_get_head_commit_ids = true;
  auto commit0 = storage_.NewCommit("id0", "content0");
  storage_.new_commits_to_return["id0"] = commit0->Clone();
  storage_.watcher_->OnNewCommits(commit0->AsList(),
                                  storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();

  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  storage_.watcher_->OnNewCommits(commit1->AsList(),
                                  storage::ChangeSource::LOCAL);
  RunLoopUntilIdle();
  ASSERT_EQ(1u, storage_.delayed_get_head_commit_ids.size());
  storage_.head_count = 1;
  storage_.delayed_get_head_commit_ids[0]();
  RunLoopUntilIdle();

  ASSERT_EQ(2u, storage_.delayed_get_head_commit_ids.size());
  storage_.delayed_get_head_commit_ids[1]();
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace cloud_sync
