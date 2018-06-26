// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/page_download.h"

#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include <lib/async/dispatcher.h>

#include "gtest/gtest.h"
#include "lib/backoff/testing/test_backoff.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_with_loop.h"
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

constexpr zx::duration kTestBackoffInterval = zx::msec(50);
std::unique_ptr<backoff::TestBackoff> NewTestBackoff() {
  auto result = std::make_unique<backoff::TestBackoff>();
  result->backoff_to_return = kTestBackoffInterval;
  return result;
}

// Dummy implementation of a backoff policy, which always returns zero backoff
// time.
template <typename E>
class BasePageDownloadTest : public gtest::TestWithLoop,
                             public PageDownload::Delegate {
 public:
  BasePageDownloadTest()
      : storage_(dispatcher()),
        encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()),
        task_runner_(dispatcher()) {
    page_download_ = std::make_unique<PageDownload>(
        &task_runner_, &storage_, &storage_, &encryption_service_,
        &page_cloud_ptr_, this, NewTestBackoff());
  }
  ~BasePageDownloadTest() override {}

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
      }
    });
    page_download_->StartDownload();
    RunLoopUntilIdle();
    SetOnNewStateCallback([] {});

    if (on_idle_called) {
      return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "The download state never reached idle.";
  }

  TestPageStorage storage_;
  E encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_ptr_;
  TestPageCloud page_cloud_;
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
  FXL_DISALLOW_COPY_AND_ASSIGN(BasePageDownloadTest);
};

using PageDownloadTest =
    BasePageDownloadTest<encryption::FakeEncryptionService>;

// Verifies that the backlog of unsynced commits is retrieved from the cloud
// provider and saved in storage.
TEST_F(PageDownloadTest, DownloadBacklog) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));

  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.position_token_to_return = MakeToken("43");

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
  page_cloud_.position_token_to_return = MakeToken("43");

  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  ASSERT_EQ(1u, page_cloud_.set_watcher_position_tokens.size());
  EXPECT_EQ("43",
            convert::ToString(
                page_cloud_.set_watcher_position_tokens.front()->opaque_id));
}

// Verifies that commit notifications about new commits in cloud provider are
// received and passed to storage.
TEST_F(PageDownloadTest, ReceiveNotifications) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  // Deliver a remote notification.
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));
  fidl::VectorPtr<cloud_provider::Commit> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id1", "content1"));
  commits.push_back(MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits), MakeToken("43"),
                                        [] {});
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
  RunLoopFor(kTestBackoffInterval);
  EXPECT_EQ(2u, page_cloud_.set_watcher_position_tokens.size());

  page_cloud_.set_watcher->OnError(cloud_provider::Status::AUTH_ERROR);
  RunLoopFor(kTestBackoffInterval);
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
  fidl::VectorPtr<cloud_provider::Commit> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits), MakeToken("42"),
                                        [] {});
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_.delayed_add_commit_confirmations.size());

  // Add two more remote commits, before storage confirms adding the first one.
  fidl::VectorPtr<cloud_provider::Commit> more_commits;
  more_commits.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  more_commits.push_back(
      MakeTestCommit(&encryption_service_, "id3", "content3"));
  page_cloud_.set_watcher->OnNewCommits(std::move(more_commits),
                                        MakeToken("44"), [] {});

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

// TODO(LE-497): The following should not pass. Investigate why.
// Verifies that failing attempts to download the backlog of unsynced commits
// are retried.
TEST_F(PageDownloadTest, RetryDownloadBacklog) {
  page_cloud_.status_to_return = cloud_provider::Status::NETWORK_ERROR;
  page_download_->StartDownload();

  // Loop through five attempts to download the backlog.
  SetOnNewStateCallback([this] {
    if (page_cloud_.get_commits_calls >= 5u) {
      QuitLoop();
    }
  });
  RunLoopUntilIdle();
  EXPECT_GE(5u, page_cloud_.get_commits_calls);
  EXPECT_EQ(0u, storage_.received_commits.size());

  SetOnNewStateCallback([] {});
  page_cloud_.status_to_return = cloud_provider::Status::OK;
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.position_token_to_return = MakeToken("42");
  RunLoopFor(kTestBackoffInterval);
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
  fidl::VectorPtr<cloud_provider::Commit> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits), MakeToken("42"),
                                        [] {});

  RunLoopUntilIdle();
  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(DOWNLOAD_PERMANENT_ERROR, states_.back());
  EXPECT_FALSE(page_cloud_.set_watcher.is_bound());
}

// Verifies that the idle status is returned when there is no download in
// progress.
TEST_F(PageDownloadTest, DownloadIdleCallback) {
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.position_token_to_return = MakeToken("43");

  int on_idle_calls = 0;
  SetOnNewStateCallback([this, &on_idle_calls] {
    if (states_.back() == DOWNLOAD_IDLE) {
      on_idle_calls++;
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
  fidl::VectorPtr<cloud_provider::Commit> commits;
  commits.push_back(MakeTestCommit(&encryption_service_, "id3", "content3"));
  page_cloud_.set_watcher->OnNewCommits(std::move(commits), MakeToken("44"),
                                        [] {});
  RunLoopUntilIdle();
  EXPECT_EQ(3u, storage_.received_commits.size());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_download_->IsIdle());
}

// Verifies that sync correctly fetches objects from the cloud provider.
TEST_F(PageDownloadTest, GetObject) {
  storage::ObjectIdentifier object_identifier{1u, 1u, "object_digest"};
  std::string object_name =
      encryption_service_.GetObjectNameSynchronous(object_identifier);
  page_cloud_.objects_to_return[object_name] =
      encryption_service_.EncryptObjectSynchronous("content");
  page_download_->StartDownload();

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data_chunk;
  RunLoopUntilIdle();
  states_.clear();
  storage_.page_sync_delegate_->GetObject(
      object_identifier, callback::Capture(callback::SetWhenCalled(&called),
                                           &status, &source, &data_chunk));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(storage::ChangeSource::CLOUD, source);
  EXPECT_EQ("content", data_chunk->Get().ToString());
  EXPECT_EQ(2u, states_.size());
  EXPECT_EQ(DOWNLOAD_IN_PROGRESS, states_[0]);
  EXPECT_EQ(DOWNLOAD_IDLE, states_[1]);
}

// Verifies that sync retries GetObject() attempts upon connection error.
TEST_F(PageDownloadTest, RetryGetObject) {
  storage::ObjectIdentifier object_identifier{1u, 1u, "object_digest"};
  std::string object_name =
      encryption_service_.GetObjectNameSynchronous(object_identifier);

  page_cloud_.status_to_return = cloud_provider::Status::NETWORK_ERROR;
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      QuitLoop();
    }
  });

  page_download_->StartDownload();

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data_chunk;
  storage_.page_sync_delegate_->GetObject(
      object_identifier, callback::Capture(callback::SetWhenCalled(&called),
                                           &status, &source, &data_chunk));

  // Allow the operation to succeed after looping through five attempts.
  RunLoopFor(kTestBackoffInterval * 4);
  page_cloud_.status_to_return = cloud_provider::Status::OK;
  page_cloud_.objects_to_return[object_name] =
      encryption_service_.EncryptObjectSynchronous("content");
  RunLoopFor(kTestBackoffInterval);

  EXPECT_TRUE(called);
  EXPECT_EQ(6u, page_cloud_.get_object_calls);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(storage::ChangeSource::CLOUD, source);
  EXPECT_EQ("content", data_chunk->Get().ToString());
}

class FailingDecryptCommitEncryptionService
    : public encryption::FakeEncryptionService {
 public:
  explicit FailingDecryptCommitEncryptionService(async_t* async)
      : encryption::FakeEncryptionService(async) {}

  void DecryptCommit(
      convert::ExtendedStringView /*storage_bytes*/,
      std::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

class FailingGetNameEncryptionService
    : public encryption::FakeEncryptionService {
 public:
  explicit FailingGetNameEncryptionService(async_t* async)
      : encryption::FakeEncryptionService(async) {}

  void GetObjectName(
      storage::ObjectIdentifier /*object_identifier*/,
      std::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

class FailingDecryptObjectEncryptionService
    : public encryption::FakeEncryptionService {
 public:
  explicit FailingDecryptObjectEncryptionService(async_t* async)
      : encryption::FakeEncryptionService(async) {}

  void DecryptObject(
      storage::ObjectIdentifier /*object_identifier*/,
      std::string /*encrypted_data*/,
      std::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

using FailingDecryptCommitPageDownloadTest =
    BasePageDownloadTest<FailingDecryptCommitEncryptionService>;
TEST_F(FailingDecryptCommitPageDownloadTest, Fail) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));

  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id1", "content1"));
  page_cloud_.commits_to_return.push_back(
      MakeTestCommit(&encryption_service_, "id2", "content2"));
  page_cloud_.position_token_to_return = MakeToken("43");

  EXPECT_FALSE(StartDownloadAndWaitForIdle());
  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(DOWNLOAD_PERMANENT_ERROR, states_.back());
}

template <typename E>
using FailingPageDownloadTest = BasePageDownloadTest<E>;

using FailingEncryptionServices =
    ::testing::Types<FailingGetNameEncryptionService,
                     FailingDecryptObjectEncryptionService>;

TYPED_TEST_CASE(FailingPageDownloadTest, FailingEncryptionServices);

TYPED_TEST(FailingPageDownloadTest, Fail) {
  storage::ObjectIdentifier object_identifier{1u, 1u, "object_digest"};
  std::string object_name =
      this->encryption_service_.GetObjectNameSynchronous(object_identifier);
  this->page_cloud_.objects_to_return[object_name] =
      this->encryption_service_.EncryptObjectSynchronous("content");
  this->page_download_->StartDownload();

  bool called;
  storage::Status status;
  storage::ChangeSource source;
  std::unique_ptr<storage::DataSource::DataChunk> data_chunk;
  this->storage_.page_sync_delegate_->GetObject(
      object_identifier, callback::Capture(callback::SetWhenCalled(&called),
                                           &status, &source, &data_chunk));
  this->RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::IO_ERROR, status);
  EXPECT_EQ(storage::ChangeSource::CLOUD, source);
}

}  // namespace
}  // namespace cloud_sync
