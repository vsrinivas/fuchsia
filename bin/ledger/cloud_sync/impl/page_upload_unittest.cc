// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/page_upload.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "apps/ledger/src/auth_provider/test/test_auth_provider.h"
#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/cloud_provider/test/test_page_cloud_handler.h"
#include "apps/ledger/src/cloud_sync/impl/constants.h"
#include "apps/ledger/src/cloud_sync/impl/test/test_page_storage.h"
#include "apps/ledger/src/cloud_sync/public/sync_state_watcher.h"
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

class PageUploadTest : public ::test::TestWithMessageLoop,
                       public PageUpload::Delegate {
 public:
  PageUploadTest()
      : storage_(&message_loop_),
        cloud_provider_(message_loop_.task_runner()),
        auth_provider_(message_loop_.task_runner()) {
    page_upload_ = std::make_unique<PageUpload>(&storage_, &cloud_provider_,
                                                &auth_provider_, this);
  }
  ~PageUploadTest() override {}

 protected:
  void SetOnNewStateCallback(fxl::Closure callback) {
    new_state_callback_ = std::move(callback);
  }

  // PageUpload::Delegate:
  void GetAuthToken(std::function<void(std::string)> on_token_ready,
                    fxl::Closure on_failed) override {
    auto request = auth_provider_.GetFirebaseToken([
      on_token_ready = std::move(on_token_ready),
      on_failed = std::move(on_failed)
    ](auth_provider::AuthStatus auth_status, std::string auth_token) {
      if (auth_status != auth_provider::AuthStatus::OK) {
        on_failed();
        return;
      }
      on_token_ready(std::move(auth_token));
    });
    auth_token_requests_.emplace(request);
  }

  void Retry(fxl::Closure callable) override {
    retry_calls_++;
    message_loop_.task_runner()->PostDelayedTask(
        std::move(callable), fxl::TimeDelta::FromMilliseconds(50));
  }

  void Success() override {}

  void SetUploadState(UploadSyncState sync_state) override {
    states_.push_back(sync_state);
    if (new_state_callback_) {
      new_state_callback_();
    }
  }

  bool IsDownloadIdle() override { return is_download_idle_; }

  test::TestPageStorage storage_;
  cloud_provider_firebase::test::TestPageCloudHandler cloud_provider_;
  auth_provider::test::TestAuthProvider auth_provider_;
  std::vector<UploadSyncState> states_;
  std::unique_ptr<PageUpload> page_upload_;
  int retry_calls_ = 0;
  bool is_download_idle_ = true;
  callback::CancellableContainer auth_token_requests_;

 private:
  fxl::Closure new_state_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageUploadTest);
};

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to PageCloudHandler.
TEST_F(PageUploadTest, UploadBacklog) {
  storage_.NewCommit("id1", "content1");
  storage_.NewCommit("id2", "content2");
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();

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

// Verifies that the backlog of commits to upload is not uploaded until there's
// only one local head.
TEST_F(PageUploadTest, UploadBacklogOnlyOnSingleHead) {
  // Verify that two local commits are not uploaded when there is two local
  // heads.
  storage_.head_count = 2;
  storage_.NewCommit("id0", "content0");
  storage_.NewCommit("id1", "content1");
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());

  // Add a new commit and reduce the number of heads to 1.
  storage_.head_count = 1;
  auto commit = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);
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

TEST_F(PageUploadTest, UploadExistingCommitsOnlyAfterBacklogDownload) {
  // Verify that two local commits are not uploaded when download is in
  // progress.
  storage_.NewCommit("local1", "content1");
  storage_.NewCommit("local2", "content2");

  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("remote3", "content3"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("remote4", "content4"), "43");
  is_download_idle_ = false;
  SetOnNewStateCallback([this] {
    if (states_.back() == UPLOAD_WAIT_REMOTE_DOWNLOAD) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());

  is_download_idle_ = true;
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

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
TEST_F(PageUploadTest, UploadNewCommits) {
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  storage_.watcher_->OnNewCommits(commit1->AsList(),
                                  storage::ChangeSource::LOCAL);

  // The commit coming from sync should be ignored.
  auto commit2 = storage_.NewCommit("id2", "content2", false);
  storage_.new_commits_to_return["id2"] = commit2->Clone();
  storage_.watcher_->OnNewCommits(commit2->AsList(),
                                  storage::ChangeSource::SYNC);

  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.new_commits_to_return["id3"] = commit3->Clone();
  storage_.watcher_->OnNewCommits(commit3->AsList(),
                                  storage::ChangeSource::LOCAL);

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
TEST_F(PageUploadTest, UploadNewCommitsOnlyOnSingleHead) {
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

  // Add a new commit when there's only one head and verify that it is
  // uploaded.
  storage_.head_count = 1;
  auto commit0 = storage_.NewCommit("id0", "content0");
  storage_.new_commits_to_return["id0"] = commit0->Clone();
  storage_.watcher_->OnNewCommits(commit0->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsIdle());
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
  storage_.watcher_->OnNewCommits(commit1->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(0u, cloud_provider_.received_commits.size());
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.count("id1"));

  // Add another commit bringing the number of heads down to one and verify that
  // both commits are uploaded.
  storage_.head_count = 1;
  auto commit2 = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit2->Clone();
  storage_.watcher_->OnNewCommits(commit2->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsIdle());
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
TEST_F(PageUploadTest, UploadExistingAndNewCommits) {
  storage_.NewCommit("id1", "content1");

  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

  auto commit = storage_.NewCommit("id2", "content2");
  storage_.new_commits_to_return["id2"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);
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
TEST_F(PageUploadTest, RetryUpload) {
  page_upload_->StartUpload();
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  SetOnNewStateCallback(nullptr);

  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  auto commit1 = storage_.NewCommit("id1", "content1");
  storage_.new_commits_to_return["id1"] = commit1->Clone();
  storage_.watcher_->OnNewCommits(commit1->AsList(),
                                  storage::ChangeSource::LOCAL);

  // Test cloud provider logs every commit, even if it reports that upload
  // failed for each. Here we loop through at least five attempts to upload the
  // commit.
  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.add_commits_calls >= 5u &&
           // We need to wait for the callback to be executed on the PageSync
           // side.
           retry_calls_ >= 5;
  }));

  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_GE(retry_calls_, 5);
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
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();

  // Verify that the idle callback is called once both commits are uploaded.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_upload_->IsIdle());

  // Notify about a new commit to upload and verify that the idle callback was
  // called again on completion.
  auto commit3 = storage_.NewCommit("id3", "content3");
  storage_.new_commits_to_return["id3"] = commit3->Clone();
  storage_.watcher_->OnNewCommits(commit3->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_upload_->IsIdle());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(3u, cloud_provider_.received_commits.size());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_upload_->IsIdle());
}

// Verifies that if listing the original commits to be uploaded fails, the
// client is notified about the error and the storage watcher is never set, so
// that subsequent commits are not handled. (as this would violate the contract
// of uploading commits in order)
TEST_F(PageUploadTest, FailToListCommits) {
  EXPECT_FALSE(storage_.watcher_set);
  int error_calls = 0;
  storage_.should_fail_get_unsynced_commits = true;
  SetOnNewStateCallback([this, &error_calls] {
    if (states_.back() == UPLOAD_ERROR) {
      error_calls++;
      message_loop_.PostQuitTask();
    }
  });

  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, error_calls);
  EXPECT_FALSE(storage_.watcher_set);
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
}

// Verifies that already synced commit are not re-uploaded.
TEST_F(PageUploadTest, DoNotUploadSyncedCommits) {
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

  auto commit = std::make_unique<test::TestCommit>("id", "content");
  storage_.new_commits_to_return["id"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);
  EXPECT_FALSE(RunLoopWithTimeout());

  // Commit is already synced.
  ASSERT_EQ(0u, cloud_provider_.received_commits.size());
}

// Verifies that commit that are received between the first upload and the retry
// are not sent.
TEST_F(PageUploadTest, DoNotUploadSyncedCommitsOnRetry) {
  SetOnNewStateCallback([this] {
    if (page_upload_->IsIdle()) {
      message_loop_.PostQuitTask();
    }
  });
  page_upload_->StartUpload();
  EXPECT_FALSE(RunLoopWithTimeout());

  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;

  auto commit = storage_.NewCommit("id", "content");
  storage_.new_commits_to_return["id"] = commit->Clone();
  storage_.watcher_->OnNewCommits(commit->AsList(),
                                  storage::ChangeSource::LOCAL);

  EXPECT_TRUE(RunLoopUntil([this] {
    // Stop once cloud provider has rejected a commit.
    return cloud_provider_.add_commits_calls > 0u;
  }));

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
}  // namespace
}  // namespace cloud_sync
