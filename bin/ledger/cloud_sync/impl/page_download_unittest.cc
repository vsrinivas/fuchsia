// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_download.h"

#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"
#include "peridot/bin/ledger/cloud_sync/impl/testing/test_page_cloud.h"
#include "peridot/bin/ledger/cloud_sync/impl/testing/test_page_storage.h"
#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/testing/commit_empty_impl.h"
#include "peridot/bin/ledger/storage/testing/page_storage_empty_impl.h"
#include "peridot/bin/ledger/testing/set_when_called.h"
#include "peridot/lib/backoff/backoff.h"
#include "peridot/lib/backoff/testing/test_backoff.h"
#include "peridot/lib/callback/capture.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace cloud_sync {
namespace {

class PageDownloadTest : public gtest::TestWithMessageLoop,
                         public PageDownload::Delegate {
 public:
  PageDownloadTest()
      : storage_(&message_loop_),
        encryption_service_(message_loop_.task_runner()),
        page_cloud_(page_cloud_ptr_.NewRequest()),
        task_runner_(message_loop_.task_runner()) {
    page_download_ = std::make_unique<PageDownload>(
        &task_runner_, &storage_, &encryption_service_, &page_cloud_ptr_, this,
        std::make_unique<backoff::TestBackoff>());
  }
  ~PageDownloadTest() override {}

 protected:
  void SetOnNewStateCallback(fxl::Closure callback) {
    new_state_callback_ = std::move(callback);
  }

  // Starts download and runs the loop until the download state is idle. Returns
  // true iff the download state went to idle as expected.
  ::testing::AssertionResult StartDownloadAndWaitForIdle() {
    bool on_idle_called = false;
    SetOnNewStateCallback([this, &on_idle_called] {
      if (states_.back() == DOWNLOAD_IDLE) {
        on_idle_called = true;
        message_loop_.PostQuitTask();
      }
    });
    page_download_->StartDownload();
    RunLoopUntilIdle();
    SetOnNewStateCallback([] {});

    if (on_idle_called) {
      return ::testing::AssertionSuccess();
    } else {
      return ::testing::AssertionFailure() <<
          "The download state never reached idle.";
    }
  }

  std::string GetData(storage::DataSource* data_source) {
    std::stringstream data;
    data_source->Get(
        [&data](std::unique_ptr<storage::DataSource::DataChunk> chunk,
                      storage::DataSource::Status status) {
          EXPECT_NE(storage::DataSource::Status::ERROR, status);
          if (status == storage::DataSource::Status::TO_BE_CONTINUED) {
            data << chunk->Get();
          }
        });
    RunLoopUntilIdle();
    return data.str();
  }

  TestPageStorage storage_;
  encryption::FakeEncryptionService encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_ptr_;
  TestPageCloud page_cloud_;
  int backoff_get_next_calls_ = 0;
  std::vector<DownloadSyncState> states_;
  std::unique_ptr<PageDownload> page_download_;
  int error_callback_calls_ = 0;

 private:
  void SetDownloadState(DownloadSyncState sync_state) override {
    if (!states_.empty() && sync_state == states_.back()) {
      // Skip identical states.
      return;
    }
    states_.push_back(sync_state);
    if (new_state_callback_) {
      new_state_callback_();
    }
  }

  fxl::Closure new_state_callback_;
  callback::ScopedTaskRunner task_runner_;
  FXL_DISALLOW_COPY_AND_ASSIGN(PageDownloadTest);
};

// Verifies that the backlog of unsynced commits is retrieved from the cloud
// provider and saved in storage.
TEST_F(PageDownloadTest, DownloadBacklog) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));

  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.position_token_to_return = convert::ToArray("43");

  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata[kTimestampKey.ToString()]);
  EXPECT_EQ(DOWNLOAD_IDLE, states_.back());
}

TEST_F(PageDownloadTest, DownloadEmptyBacklog) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());
}

// Verifies that the cloud watcher is registered for the timestamp of the most
// recent commit downloaded from the backlog.
TEST_F(PageDownloadTest, RegisterWatcher) {
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.position_token_to_return = convert::ToArray("43");

  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  ASSERT_EQ(1u, page_cloud_.set_watcher_position_tokens.size());
  EXPECT_EQ("43", page_cloud_.set_watcher_position_tokens.front());
}

// Verifies that commit notifications about new commits in cloud provider are
// received and passed to storage.
TEST_F(PageDownloadTest, ReceiveNotifications) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  // Deliver a remote notification.
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));
  fidl::Array<cloud_provider::CommitPtr> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id1", "content1"));
  commits.push_back(MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits),
                                        convert::ToArray("43"), [] {});
  RunLoopUntilIdle();

  // Verify that the remote commits were added to storage.
  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata[kTimestampKey.ToString()]);
}

// Verify that we retry setting the remote watcher on connection errors
// and when the auth token expires.
TEST_F(PageDownloadTest, RetryRemoteWatcher) {
  page_download_->StartDownload();
  EXPECT_EQ(0u, storage_.received_commits.size());

  RunLoopUntilIdle();
  EXPECT_EQ(1u, page_cloud_.set_watcher_position_tokens.size());

  page_cloud_.set_watcher->OnError(cloud_provider::Status::NETWORK_ERROR);
  RunLoopUntilIdle();
  EXPECT_EQ(2u, page_cloud_.set_watcher_position_tokens.size());

  page_cloud_.set_watcher->OnError(cloud_provider::Status::AUTH_ERROR);
  RunLoopUntilIdle();
  EXPECT_EQ(3u, page_cloud_.set_watcher_position_tokens.size());
}

// Verifies that if multiple remote commits are received while one batch is
// already being downloaded, the new remote commits are added to storage in one
// request.
TEST_F(PageDownloadTest, CoalesceMultipleNotifications) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  // Make the storage delay requests to add remote commits.
  storage_.should_delay_add_commit_confirmation = true;

  // Deliver a remote notification.
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));
  fidl::Array<cloud_provider::CommitPtr> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits),
                                        convert::ToArray("42"), [] {});
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_.delayed_add_commit_confirmations.size());

  // Add two more remote commits, before storage confirms adding the first one.
  fidl::Array<cloud_provider::CommitPtr> more_commits;
  more_commits.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  more_commits.push_back(
      MakeTestCommit(&encryption_service_, "id3", "content3"));
  page_cloud_.set_watcher->OnNewCommits(std::move(more_commits),
                                        convert::ToArray("44"), [] {});

  // Make storage confirm adding the first commit.
  storage_.should_delay_add_commit_confirmation = false;
  storage_.delayed_add_commit_confirmations.front()();
  RunLoopUntilIdle();
  EXPECT_EQ(3u, storage_.received_commits.size());

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
TEST_F(PageDownloadTest, RetryDownloadBacklog) {
  page_cloud_.status_to_return = cloud_provider::Status::NETWORK_ERROR;
  page_download_->StartDownload();

  // Loop through five attempts to download the backlog.
  SetOnNewStateCallback([this] {
    if (page_cloud_.get_commits_calls >= 5u) {
      message_loop_.PostQuitTask();
    }
  });
  RunLoopUntilIdle();
  EXPECT_GE(5u, page_cloud_.get_commits_calls);
  EXPECT_EQ(0u, storage_.received_commits.size());

  SetOnNewStateCallback([] {});
  page_cloud_.status_to_return = cloud_provider::Status::OK;
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.position_token_to_return = convert::ToArray("42");
  RunLoopUntilIdle();
  EXPECT_TRUE(page_download_->IsIdle());

  EXPECT_EQ(1u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("42", storage_.sync_metadata[kTimestampKey.ToString()]);
}

// Verifies that a failure to persist the remote commit stops syncing remote
// commits and the error status is returned.
TEST_F(PageDownloadTest, FailToStoreRemoteCommit) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());
  EXPECT_TRUE(page_cloud_.set_watcher.is_bound());

  storage_.should_fail_add_commit_from_sync = true;
  fidl::Array<cloud_provider::CommitPtr> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits),
                                        convert::ToArray("42"), [] {});

  RunLoopUntilIdle();
  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(DOWNLOAD_PERMANENT_ERROR, states_.back());
  EXPECT_TRUE(page_cloud_.set_watcher.encountered_error());
}

// Verifies that the idle status is returned when there is no download in
// progress.
TEST_F(PageDownloadTest, DownloadIdleCallback) {
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.position_token_to_return = convert::ToArray("43");

  int on_idle_calls = 0;
  SetOnNewStateCallback([this, &on_idle_calls] {
    if (states_.back() == DOWNLOAD_IDLE) {
      on_idle_calls++;
      message_loop_.PostQuitTask();
    }
  });
  page_download_->StartDownload();
  EXPECT_EQ(0, on_idle_calls);
  EXPECT_FALSE(page_download_->IsIdle());

  // Run the message loop and verify that the sync is idle after all remote
  // commits are added to storage.
  RunLoopUntilIdle();
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_download_->IsIdle());

  // Notify about a new commit to download and verify that the idle callback was
  // called again on completion.
  fidl::Array<cloud_provider::CommitPtr> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id3", "content3"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits),
                                        convert::ToArray("44"), [] {});
  RunLoopUntilIdle();
  EXPECT_EQ(3u, storage_.received_commits.size());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_download_->IsIdle());
}

// Verifies that sync correctly fetches objects from the cloud provider.
TEST_F(PageDownloadTest, GetObject) {
  page_cloud_.objects_to_return["object_digest"] = "content";
  page_download_->StartDownload();

  bool called;
  storage::Status status;
  std::unique_ptr<storage::DataSource> data_source;
  storage_.page_sync_delegate_->GetObject(
      storage::ObjectDigestView("object_digest"),
      callback::Capture(ledger::SetWhenCalled(&called), &status, &data_source));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(7u, data_source->GetSize());
  EXPECT_EQ("content", GetData(data_source.get()));
}

// Verifies that sync retries GetObject() attempts upon connection error.
TEST_F(PageDownloadTest, RetryGetObject) {
  page_cloud_.status_to_return = cloud_provider::Status::NETWORK_ERROR;
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      message_loop_.PostQuitTask();
    }
  });

  page_download_->StartDownload();

  message_loop_.SetAfterTaskCallback([this] {
    // Allow the operation to succeed after looping through five attempts.
    if (page_cloud_.get_object_calls == 5u) {
      page_cloud_.status_to_return = cloud_provider::Status::OK;
      page_cloud_.objects_to_return["object_digest"] = "content";
    }
  });
  bool called;
  storage::Status status;
  std::unique_ptr<storage::DataSource> data_source;
  storage_.page_sync_delegate_->GetObject(
      storage::ObjectDigestView("object_digest"),
      callback::Capture(ledger::SetWhenCalled(&called), &status, &data_source));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(6u, page_cloud_.get_object_calls);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(7u, data_source->GetSize());
  EXPECT_EQ("content", GetData(data_source.get()));
}

}  // namespace
}  // namespace cloud_sync
