// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_download.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/auth_provider/test/test_auth_provider.h"
#include "peridot/bin/ledger/backoff/backoff.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/cloud_provider/test/test_page_cloud_handler.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"
#include "peridot/bin/ledger/cloud_sync/impl/test/test_page_storage.h"
#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/test/commit_empty_impl.h"
#include "peridot/bin/ledger/storage/test/page_storage_empty_impl.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace cloud_sync {
namespace {

class PageDownloadTest : public ::test::TestWithMessageLoop,
                         public PageDownload::Delegate {
 public:
  PageDownloadTest()
      : storage_(&message_loop_),
        cloud_provider_(message_loop_.task_runner()),
        auth_provider_(message_loop_.task_runner()),
        task_runner_(message_loop_.task_runner()) {
    page_download_ = std::make_unique<PageDownload>(&task_runner_, &storage_,
                                                    &cloud_provider_, this);
  }
  ~PageDownloadTest() override {}

 protected:
  void SetOnNewStateCallback(fxl::Closure callback) {
    new_state_callback_ = std::move(callback);
  }

  test::TestPageStorage storage_;
  cloud_provider_firebase::test::TestPageCloudHandler cloud_provider_;
  auth_provider::test::TestAuthProvider auth_provider_;
  int backoff_get_next_calls_ = 0;
  std::vector<DownloadSyncState> states_;
  std::unique_ptr<PageDownload> page_download_;
  int error_callback_calls_ = 0;
  callback::CancellableContainer auth_token_requests_;

 private:
  // PageDownload::Delegate:
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
    message_loop_.task_runner()->PostDelayedTask(
        std::move(callable), fxl::TimeDelta::FromMilliseconds(50));
  }

  void Success() override {}

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

  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");

  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_IDLE) {
      message_loop_.PostQuitTask();
    }
  });
  auth_provider_.token_to_return = "some-token";
  page_download_->StartDownload();

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata[kTimestampKey.ToString()]);
  EXPECT_EQ(DOWNLOAD_IDLE, states_.back());
  EXPECT_EQ(std::vector<std::string>{"some-token"},
            cloud_provider_.get_commits_auth_tokens);
}

// Verifies that if auth provider fails to provide the auth token, the error
// status is returned.
TEST_F(PageDownloadTest, DownloadBacklogAuthError) {
  auth_provider_.status_to_return = auth_provider::AuthStatus::ERROR;
  auth_provider_.token_to_return = "";
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      message_loop_.PostQuitTask();
    }
  });
  page_download_->StartDownload();
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(std::vector<std::string>{},
            cloud_provider_.get_commits_auth_tokens);
}

// Verifies that callbacks are correctly run after downloading an empty backlog
// of remote commits.
TEST_F(PageDownloadTest, DownloadEmptyBacklog) {
  int on_idle_calls = 0;
  SetOnNewStateCallback([this, &on_idle_calls] {
    if (states_.back() == DOWNLOAD_IDLE) {
      on_idle_calls++;
      message_loop_.PostQuitTask();
    }
  });
  page_download_->StartDownload();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_idle_calls);
}

// Verifies that the cloud watcher is registered for the timestamp of the most
// recent commit downloaded from the backlog.
TEST_F(PageDownloadTest, RegisterWatcher) {
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");
  auth_provider_.token_to_return = "some-token";

  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_IDLE) {
      message_loop_.PostQuitTask();
    }
  });
  page_download_->StartDownload();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(std::vector<std::string>{"some-token"},
            cloud_provider_.watch_commits_auth_tokens);
  ASSERT_EQ(1u, cloud_provider_.watch_call_min_timestamps.size());
  EXPECT_EQ("43", cloud_provider_.watch_call_min_timestamps.front());
}

// Verifies that if auth provider fails to provide the auth token, the watcher
// is not set and the error callback is called.
TEST_F(PageDownloadTest, RegisterWatcherAuthError) {
  auth_provider_.status_to_return = auth_provider::AuthStatus::ERROR;
  auth_provider_.token_to_return = "";
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      message_loop_.PostQuitTask();
    }
  });
  page_download_->StartDownload();
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(0u, cloud_provider_.watch_call_min_timestamps.size());
}

// Verifies that commit notifications about new commits in cloud provider are
// received and passed to storage.
TEST_F(PageDownloadTest, ReceiveNotifications) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));

  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");
  page_download_->StartDownload();

  EXPECT_TRUE(
      RunLoopUntil([this] { return storage_.received_commits.size() == 2u; }));

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

  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.watch_call_min_timestamps.size() == 1u;
  }));

  cloud_provider_.watcher->OnConnectionError();
  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.watch_call_min_timestamps.size() == 2u;
  }));

  cloud_provider_.watcher->OnTokenExpired();
  EXPECT_TRUE(RunLoopUntil([this] {
    return cloud_provider_.watch_call_min_timestamps.size() == 3u;
  }));
}

// Verifies that if multiple remote commits are received while one batch is
// already being downloaded, the new remote commits are added to storage in one
// request.
TEST_F(PageDownloadTest, CoalesceMultipleNotifications) {
  EXPECT_EQ(0u, storage_.received_commits.size());

  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");

  // Make the storage delay requests to add remote commits.
  storage_.should_delay_add_commit_confirmation = true;
  page_download_->StartDownload();
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

  // Add two more remote commits, before storage confirms adding the first one.
  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");
  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id3", "content3"), "44");
  cloud_provider_.DeliverRemoteCommits();

  // Make storage confirm adding the first commit.
  storage_.should_delay_add_commit_confirmation = false;
  storage_.delayed_add_commit_confirmations.front()();
  EXPECT_TRUE(
      RunLoopUntil([this] { return storage_.received_commits.size() == 3u; }));

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
  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  page_download_->StartDownload();

  // Loop through five attempts to download the backlog.
  EXPECT_TRUE(
      RunLoopUntil([this] { return cloud_provider_.get_commits_calls >= 5u; }));
  EXPECT_EQ(0u, storage_.received_commits.size());

  cloud_provider_.status_to_return = cloud_provider_firebase::Status::OK;
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  EXPECT_TRUE(
      RunLoopUntil([this] { return storage_.received_commits.size() == 1u; }));

  EXPECT_EQ(1u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("42", storage_.sync_metadata[kTimestampKey.ToString()]);
}

// Verifies that a failure to persist the remote commit stops syncing remote
// commits and the error status is returned.
TEST_F(PageDownloadTest, FailToStoreRemoteCommit) {
  EXPECT_FALSE(cloud_provider_.watcher_removed);

  cloud_provider_.notifications_to_deliver.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  storage_.should_fail_add_commit_from_sync = true;

  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      message_loop_.PostQuitTask();
    }
  });

  page_download_->StartDownload();
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(cloud_provider_.watcher_removed);
}

// Verifies that the idle status is returned when there is no download in
// progress.
TEST_F(PageDownloadTest, DownloadIdleCallback) {
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id1", "content1"), "42");
  cloud_provider_.records_to_return.emplace_back(
      cloud_provider_firebase::Commit("id2", "content2"), "43");

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
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_download_->IsIdle());

  // Notify about a new commit to download and verify that the idle callback was
  // called again on completion.
  std::vector<cloud_provider_firebase::Record> records;
  records.emplace_back(cloud_provider_firebase::Commit("id3", "content3"),
                       "44");
  cloud_provider_.watcher->OnRemoteCommits(std::move(records));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(3u, storage_.received_commits.size());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_download_->IsIdle());
}

// Verifies that sync correctly fetches objects from the cloud provider.
TEST_F(PageDownloadTest, GetObject) {
  cloud_provider_.objects_to_return["object_id"] = "content";
  auth_provider_.token_to_return = "some-token";
  page_download_->StartDownload();

  storage::Status status;
  uint64_t size;
  zx::socket data;
  storage_.page_sync_delegate_->GetObject(
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
TEST_F(PageDownloadTest, GetObjectAuthError) {
  cloud_provider_.objects_to_return["object_id"] = "content";
  auth_provider_.token_to_return = "some-token";
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_IDLE) {
      message_loop_.PostQuitTask();
    }
  });
  page_download_->StartDownload();
  EXPECT_FALSE(RunLoopWithTimeout());

  auth_provider_.status_to_return = auth_provider::AuthStatus::ERROR;
  auth_provider_.token_to_return = "";
  storage::Status status;
  uint64_t size;
  zx::socket data;
  storage_.page_sync_delegate_->GetObject(
      storage::ObjectIdView("object_id"),
      callback::Capture(MakeQuitTask(), &status, &size, &data));
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(0, error_callback_calls_);
  EXPECT_EQ(storage::Status::IO_ERROR, status);
  EXPECT_EQ(std::vector<std::string>{}, cloud_provider_.get_object_auth_tokens);
  EXPECT_FALSE(data);
}

// Verifies that sync retries GetObject() attempts upon connection error.
TEST_F(PageDownloadTest, RetryGetObject) {
  cloud_provider_.status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      message_loop_.PostQuitTask();
    }
  });

  page_download_->StartDownload();

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
  storage_.page_sync_delegate_->GetObject(
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

}  // namespace
}  // namespace cloud_sync
