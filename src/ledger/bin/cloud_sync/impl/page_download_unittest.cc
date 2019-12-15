// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/page_download.h"

#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/cloud_sync/impl/constants.h"
#include "src/ledger/bin/cloud_sync/impl/entry_payload_encoding.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"
#include "src/ledger/bin/cloud_sync/public/sync_state_watcher.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/page_sync_client.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/backoff/testing/test_backoff.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/socket/strings.h"

namespace cloud_sync {
namespace {

using ::testing::AnyOf;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::SizeIs;

// Creates a dummy continuation token.
cloud_provider::PositionToken MakeToken(convert::ExtendedStringView token_id) {
  cloud_provider::PositionToken token;
  token.opaque_id = convert::ToArray(token_id);
  return token;
}

// Creates a dummy object identifier.
storage::ObjectIdentifier MakeObjectIdentifier() {
  // The returned value does not need to be valid (wrt. internal storage constraints) as it is only
  // used as an opaque identifier for cloud_sync. It does not need to be tracked either because we
  // use |TestPageStorage|, a fake storage that does not perform garbage collection.
  return storage::ObjectIdentifier(1u, storage::ObjectDigest("object_digest"), nullptr);
}

// Dummy implementation of a backoff policy, which always returns constant backoff
// time.
constexpr zx::duration kTestBackoffInterval = zx::msec(50);
std::unique_ptr<ledger::TestBackoff> NewTestBackoff() {
  auto result = std::make_unique<ledger::TestBackoff>(kTestBackoffInterval);
  return result;
}

class FakePageSyncDelegate : public storage::PageSyncDelegate {
 public:
  explicit FakePageSyncDelegate(PageDownload* page_download) : page_download_(page_download) {}
  ~FakePageSyncDelegate() override = default;

  void GetObject(storage::ObjectIdentifier object_identifier,
                 storage::RetrievedObjectType retrieved_object_type,
                 fit::function<void(ledger::Status, storage::ChangeSource, storage::IsObjectSynced,
                                    std::unique_ptr<storage::DataSource::DataChunk>)>
                     callback) override {
    page_download_->GetObject(std::move(object_identifier), retrieved_object_type,
                              std::move(callback));
  }
  void GetDiff(
      storage::CommitId commit_id, std::vector<storage::CommitId> possible_bases,
      fit::function<void(ledger::Status, storage::CommitId, std::vector<storage::EntryChange>)>
          callback) override {
    page_download_->GetDiff(std::move(commit_id), std::move(possible_bases), std::move(callback));
  }

  void UpdateClock(storage::Clock /*clock*/,
                   fit::function<void(ledger::Status)> /*callback*/) override {
    LEDGER_NOTIMPLEMENTED();
  }

 private:
  PageDownload* const page_download_;
};

// Dummy implementation of a backoff policy, which always returns zero backoff
// time.
template <typename E>
class BasePageDownloadTest : public ledger::TestWithEnvironment, public PageDownload::Delegate {
 public:
  BasePageDownloadTest()
      : storage_(dispatcher()),
        encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()),
        task_runner_(dispatcher()) {
    page_download_ = std::make_unique<PageDownload>(&task_runner_, &storage_, &encryption_service_,
                                                    &page_cloud_ptr_, this, NewTestBackoff());
    sync_delegate_ = std::make_unique<FakePageSyncDelegate>(page_download_.get());
    storage_.SetSyncDelegate(sync_delegate_.get());
  }
  BasePageDownloadTest(const BasePageDownloadTest&) = delete;
  BasePageDownloadTest& operator=(const BasePageDownloadTest&) = delete;
  ~BasePageDownloadTest() override = default;

 protected:
  void SetOnNewStateCallback(fit::closure callback) { new_state_callback_ = std::move(callback); }

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
    return ::testing::AssertionFailure() << "The download state never reached idle.";
  }

  TestPageStorage storage_;
  E encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_ptr_;
  TestPageCloud page_cloud_;
  std::vector<DownloadSyncState> states_;
  std::unique_ptr<PageDownload> page_download_;
  std::unique_ptr<FakePageSyncDelegate> sync_delegate_;
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

  fit::closure new_state_callback_;
  callback::ScopedTaskRunner task_runner_;
};

using PageDownloadTest = BasePageDownloadTest<encryption::FakeEncryptionService>;

// Verifies that the backlog of unsynced commits is retrieved from the cloud
// provider and saved in storage.
TEST_F(PageDownloadTest, DownloadBacklog) {
  EXPECT_EQ(storage_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.sync_metadata.count(convert::ToString(kTimestampKey)), 0u);

  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content1"));
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content2"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("43"));

  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  EXPECT_EQ(storage_.received_commits.size(), 2u);
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content1"));
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content2"));
  EXPECT_EQ(storage_.sync_metadata[convert::ToString(kTimestampKey)], "43");
  EXPECT_EQ(states_.back(), DOWNLOAD_IDLE);
}

TEST_F(PageDownloadTest, DownloadLongBacklog) {
  EXPECT_EQ(storage_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.sync_metadata.count(convert::ToString(kTimestampKey)), 0u);

  const size_t commit_count = 100'000;
  for (size_t i = 0; i < commit_count; i++) {
    page_cloud_.commits_to_return.push_back(
        MakeTestCommit(&encryption_service_, "content" + std::to_string(i)));
  }
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("43"));

  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  EXPECT_EQ(storage_.received_commits.size(), commit_count);
  EXPECT_EQ(storage_.sync_metadata[convert::ToString(kTimestampKey)], "43");
  EXPECT_EQ(states_.back(), DOWNLOAD_IDLE);
}

TEST_F(PageDownloadTest, DownloadEmptyBacklog) { ASSERT_TRUE(StartDownloadAndWaitForIdle()); }

// Verifies that the cloud watcher is registered for the timestamp of the most
// recent commit downloaded from the backlog.
TEST_F(PageDownloadTest, RegisterWatcher) {
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content1"));
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content2"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("43"));

  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  ASSERT_EQ(page_cloud_.set_watcher_position_tokens.size(), 1u);
  EXPECT_EQ(convert::ToString(page_cloud_.set_watcher_position_tokens.front()->opaque_id), "43");
}

// Verifies that commit notifications about new commits in cloud provider are
// received and passed to storage.
TEST_F(PageDownloadTest, ReceiveNotifications) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  // Deliver a remote notification.
  EXPECT_EQ(storage_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.sync_metadata.count(convert::ToString(kTimestampKey)), 0u);
  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content1", "content2"});
  ASSERT_TRUE(commit_pack);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("43"), [] {});
  RunLoopUntilIdle();

  // Verify that the remote commits were added to storage.
  EXPECT_EQ(storage_.received_commits.size(), 2u);
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content1"));
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content2"));
  EXPECT_EQ(storage_.sync_metadata[convert::ToString(kTimestampKey)], "43");
}

// Verify that we retry setting the remote watcher on connection errors
// and when the auth token expires.
TEST_F(PageDownloadTest, RetryRemoteWatcher) {
  page_download_->StartDownload();
  EXPECT_EQ(storage_.received_commits.size(), 0u);

  RunLoopUntilIdle();
  EXPECT_EQ(page_cloud_.set_watcher_position_tokens.size(), 1u);

  page_cloud_.set_watcher->OnError(cloud_provider::Status::NETWORK_ERROR);
  RunLoopFor(kTestBackoffInterval);
  EXPECT_EQ(page_cloud_.set_watcher_position_tokens.size(), 2u);

  page_cloud_.set_watcher->OnError(cloud_provider::Status::AUTH_ERROR);
  RunLoopFor(kTestBackoffInterval);
  EXPECT_EQ(page_cloud_.set_watcher_position_tokens.size(), 3u);
}

// Verifies that if multiple remote commits are received while one batch is
// already being downloaded, the new remote commits are added to storage in one
// request.
TEST_F(PageDownloadTest, CoalesceMultipleNotifications) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());

  // Make the storage delay requests to add remote commits.
  storage_.should_delay_add_commit_confirmation = true;

  // Deliver a remote notification.
  EXPECT_EQ(storage_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.sync_metadata.count(convert::ToString(kTimestampKey)), 0u);
  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content1"});
  ASSERT_TRUE(commit_pack);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("42"), [] {});
  RunLoopUntilIdle();
  EXPECT_EQ(storage_.delayed_add_commit_confirmations.size(), 1u);

  // Add two more remote commits, before storage confirms adding the first one.
  commit_pack = MakeTestCommitPack(&encryption_service_, {"content2", "content3"});
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("44"), [] {});

  // Make storage confirm adding the first commit.
  storage_.should_delay_add_commit_confirmation = false;
  storage_.delayed_add_commit_confirmations.front()();
  RunLoopUntilIdle();
  EXPECT_EQ(storage_.received_commits.size(), 3u);

  // Verify that all three commits were delivered in total of two calls to
  // storage.
  EXPECT_EQ(storage_.received_commits.size(), 3u);
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content1"));
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content2"));
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content3"));
  EXPECT_EQ(storage_.sync_metadata[convert::ToString(kTimestampKey)], "44");
  EXPECT_EQ(storage_.add_commits_from_sync_calls, 2u);
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
  EXPECT_EQ(storage_.received_commits.size(), 0u);
  EXPECT_TRUE(page_download_->IsPaused());
  EXPECT_FALSE(page_download_->IsIdle());

  SetOnNewStateCallback([] {});
  page_cloud_.status_to_return = cloud_provider::Status::OK;
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content1"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("42"));
  RunLoopFor(kTestBackoffInterval);
  EXPECT_TRUE(page_download_->IsPaused());
  EXPECT_TRUE(page_download_->IsIdle());

  EXPECT_EQ(storage_.received_commits.size(), 1u);
  EXPECT_TRUE(storage_.ReceivedCommitsContains("content1"));
  EXPECT_EQ(storage_.sync_metadata[convert::ToString(kTimestampKey)], "42");
}

// Verifies that a failure to persist the remote commit stops syncing remote
// commits and the error status is returned.
TEST_F(PageDownloadTest, FailToStoreRemoteCommit) {
  ASSERT_TRUE(StartDownloadAndWaitForIdle());
  EXPECT_TRUE(page_cloud_.set_watcher.is_bound());

  storage_.should_fail_add_commit_from_sync = true;
  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content1"});
  ASSERT_TRUE(commit_pack);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("42"), [] {});

  RunLoopUntilIdle();
  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(states_.back(), DOWNLOAD_PERMANENT_ERROR);
  EXPECT_FALSE(page_cloud_.set_watcher.is_bound());
}

// Verifies that the idle status is returned when there is no download in
// progress.
TEST_F(PageDownloadTest, DownloadIdleCallback) {
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content1"));
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content2"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("43"));

  int on_idle_calls = 0;
  SetOnNewStateCallback([this, &on_idle_calls] {
    if (states_.back() == DOWNLOAD_IDLE) {
      on_idle_calls++;
    }
  });
  page_download_->StartDownload();
  EXPECT_EQ(on_idle_calls, 0);
  EXPECT_FALSE(page_download_->IsIdle());

  // Run the message loop and verify that the sync is idle after all remote
  // commits are added to storage.
  RunLoopUntilIdle();
  EXPECT_EQ(on_idle_calls, 1);
  EXPECT_TRUE(page_download_->IsIdle());

  // Notify about a new commit to download and verify that the idle callback was
  // called again on completion.
  auto commit_pack = MakeTestCommitPack(&encryption_service_, {"content3"});
  ASSERT_TRUE(commit_pack);
  page_cloud_.set_watcher->OnNewCommits(std::move(*commit_pack), MakeToken("44"), [] {});
  RunLoopUntilIdle();
  EXPECT_EQ(storage_.received_commits.size(), 3u);
  EXPECT_EQ(on_idle_calls, 2);
  EXPECT_TRUE(page_download_->IsIdle());
}

// Verifies that sync correctly fetches objects from the cloud provider.
TEST_F(PageDownloadTest, GetObject) {
  storage::ObjectIdentifier object_identifier = MakeObjectIdentifier();
  std::string object_name = encryption_service_.GetObjectNameSynchronous(object_identifier);
  page_cloud_.objects_to_return[object_name] =
      encryption_service_.EncryptObjectSynchronous("content");
  page_download_->StartDownload();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data_chunk;
  RunLoopUntilIdle();
  states_.clear();
  storage_.page_sync_delegate_->GetObject(object_identifier, storage::RetrievedObjectType::BLOB,
                                          ledger::Capture(ledger::SetWhenCalled(&called), &status,
                                                          &source, &is_object_synced, &data_chunk));
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(source, storage::ChangeSource::CLOUD);
  EXPECT_EQ(is_object_synced, storage::IsObjectSynced::YES);
  EXPECT_EQ(data_chunk->Get(), "content");
  EXPECT_THAT(states_, ElementsAre(DOWNLOAD_IN_PROGRESS, DOWNLOAD_IDLE));
}

// Verifies that sync retries GetObject() attempts upon connection error.
TEST_F(PageDownloadTest, RetryGetObject) {
  storage::ObjectIdentifier object_identifier = MakeObjectIdentifier();
  std::string object_name = encryption_service_.GetObjectNameSynchronous(object_identifier);

  page_cloud_.status_to_return = cloud_provider::Status::NETWORK_ERROR;
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      QuitLoop();
    }
  });

  page_download_->StartDownload();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data_chunk;
  storage_.page_sync_delegate_->GetObject(object_identifier, storage::RetrievedObjectType::BLOB,
                                          ledger::Capture(ledger::SetWhenCalled(&called), &status,
                                                          &source, &is_object_synced, &data_chunk));

  // Allow the operation to succeed after looping through five attempts.
  RunLoopFor(kTestBackoffInterval * 4);
  page_cloud_.status_to_return = cloud_provider::Status::OK;
  page_cloud_.objects_to_return[object_name] =
      encryption_service_.EncryptObjectSynchronous("content");
  RunLoopFor(kTestBackoffInterval);

  ASSERT_TRUE(called);
  EXPECT_EQ(page_cloud_.get_object_calls, 6u);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(source, storage::ChangeSource::CLOUD);
  EXPECT_EQ(data_chunk->Get(), "content");
  EXPECT_EQ(is_object_synced, storage::IsObjectSynced::YES);
}

class FailingDecryptCommitEncryptionService : public encryption::FakeEncryptionService {
 public:
  explicit FailingDecryptCommitEncryptionService(async_dispatcher_t* dispatcher)
      : encryption::FakeEncryptionService(dispatcher) {}

  void DecryptCommit(convert::ExtendedStringView /*storage_bytes*/,
                     fit::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

class FailingGetNameEncryptionService : public encryption::FakeEncryptionService {
 public:
  explicit FailingGetNameEncryptionService(async_dispatcher_t* dispatcher)
      : encryption::FakeEncryptionService(dispatcher) {}

  void GetObjectName(storage::ObjectIdentifier /*object_identifier*/,
                     fit::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

class FailingDecryptObjectEncryptionService : public encryption::FakeEncryptionService {
 public:
  explicit FailingDecryptObjectEncryptionService(async_dispatcher_t* dispatcher)
      : encryption::FakeEncryptionService(dispatcher) {}

  void DecryptObject(storage::ObjectIdentifier /*object_identifier*/,
                     std::string /*encrypted_data*/,
                     fit::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

using FailingDecryptCommitPageDownloadTest =
    BasePageDownloadTest<FailingDecryptCommitEncryptionService>;
TEST_F(FailingDecryptCommitPageDownloadTest, Fail) {
  EXPECT_EQ(storage_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.sync_metadata.count(convert::ToString(kTimestampKey)), 0u);

  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content1"));
  page_cloud_.commits_to_return.push_back(MakeTestCommit(&encryption_service_, "content2"));
  page_cloud_.position_token_to_return = fidl::MakeOptional(MakeToken("43"));

  EXPECT_FALSE(StartDownloadAndWaitForIdle());
  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(states_.back(), DOWNLOAD_PERMANENT_ERROR);
}

template <typename E>
using FailingPageDownloadTest = BasePageDownloadTest<E>;

using FailingEncryptionServices =
    ::testing::Types<FailingGetNameEncryptionService, FailingDecryptObjectEncryptionService>;

TYPED_TEST_SUITE(FailingPageDownloadTest, FailingEncryptionServices);

TYPED_TEST(FailingPageDownloadTest, Fail) {
  storage::ObjectIdentifier object_identifier = MakeObjectIdentifier();
  std::string object_name = this->encryption_service_.GetObjectNameSynchronous(object_identifier);
  this->page_cloud_.objects_to_return[object_name] =
      this->encryption_service_.EncryptObjectSynchronous("content");
  this->page_download_->StartDownload();

  bool called;
  ledger::Status status;
  storage::ChangeSource source;
  storage::IsObjectSynced is_object_synced;
  std::unique_ptr<storage::DataSource::DataChunk> data_chunk;
  this->storage_.page_sync_delegate_->GetObject(
      object_identifier, storage::RetrievedObjectType::BLOB,
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &source, &is_object_synced,
                      &data_chunk));
  this->RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
  EXPECT_EQ(source, storage::ChangeSource::CLOUD);
}

template <typename E>
class BasePageDownloadDiffTest
    : public BasePageDownloadTest<E>,
      public ::testing::WithParamInterface<std::function<void(cloud_provider::Diff*)>> {
 public:
  void SetUp() override {
    this->page_download_->StartDownload();
    this->RunLoopUntilIdle();
    LEDGER_DCHECK(this->states_.back() == DOWNLOAD_IDLE);
    this->states_.clear();
  }

  cloud_provider::DiffEntry MakeDiffEntry(const storage::EntryChange& change) {
    cloud_provider::DiffEntry entry;
    entry.set_entry_id(convert::ToArray(change.entry.entry_id));
    entry.set_operation(change.deleted ? cloud_provider::Operation::DELETION
                                       : cloud_provider::Operation::INSERTION);
    entry.set_data(convert::ToArray(this->encryption_service_.EncryptEntryPayloadSynchronous(
        EncodeEntryPayload(change.entry, this->storage_.GetObjectIdentifierFactory()))));
    return entry;
  }

  // Create a diff from the empty page with the given changes, with randomly shuffled changes.
  cloud_provider::Diff MakeShuffledDiff(const std::vector<storage::EntryChange>& changes) {
    std::vector<cloud_provider::DiffEntry> entries;
    std::transform(changes.begin(), changes.end(), std::back_inserter(entries),
                   [this](const storage::EntryChange& change) { return MakeDiffEntry(change); });
    std::shuffle(entries.begin(), entries.end(),
                 this->environment_.random()->template NewBitGenerator<uint64_t>());
    cloud_provider::Diff diff;
    diff.mutable_base_state()->set_empty_page({});
    diff.set_changes(std::move(entries));
    return diff;
  }

  // Generates a test diff and the expected entry changes after normalization.
  std::pair<cloud_provider::Diff, std::vector<storage::EntryChange>> MakeTestDiff() {
    cloud_provider::Diff diff;
    std::vector<storage::EntryChange> changes;

    diff.mutable_base_state()->set_at_commit(ToRemoteId("base1"));

    // The deletion will appear before the insertion after normalization.
    changes.push_back(
        {{"key1", storage::ObjectIdentifier(0u, storage::ObjectDigest("digest2"), nullptr),
          storage::KeyPriority::LAZY, "entry2"},
         /*deleted*/ true});
    changes.push_back(
        {{"key1", storage::ObjectIdentifier(1u, storage::ObjectDigest("digest1"), nullptr),
          storage::KeyPriority::EAGER, "entry1"},
         /*deleted*/ false});
    diff.mutable_changes()->push_back(MakeDiffEntry(changes[1]));
    diff.mutable_changes()->push_back(MakeDiffEntry(changes[0]));

    return {std::move(diff), std::move(changes)};
  }

  std::vector<uint8_t> ToRemoteId(const storage::CommitId& id) {
    return convert::ToArray(this->encryption_service_.EncodeCommitId(id));
  }
};

using PageDownloadDiffTest = BasePageDownloadDiffTest<encryption::FakeEncryptionService>;

TEST_F(PageDownloadDiffTest, GetDiff) {
  std::vector<storage::EntryChange> expected_changes;
  std::tie(page_cloud_.diff_to_return, expected_changes) = MakeTestDiff();
  storage_.remote_id_to_commit_id[encryption_service_.EncodeCommitId("base1")] = "base1";

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(base_commit, "base1");
  EXPECT_EQ(changes, expected_changes);

  EXPECT_THAT(states_, ElementsAre(DOWNLOAD_IN_PROGRESS, DOWNLOAD_IDLE));
}

TEST_F(PageDownloadDiffTest, GetDiffFromEmpty) {
  std::vector<storage::EntryChange> expected_changes;
  std::tie(page_cloud_.diff_to_return, expected_changes) = MakeTestDiff();
  page_cloud_.diff_to_return.mutable_base_state()->set_empty_page({});

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(base_commit, convert::ToString(storage::kFirstPageCommitId));
  EXPECT_EQ(changes, expected_changes);

  EXPECT_THAT(states_, ElementsAre(DOWNLOAD_IN_PROGRESS, DOWNLOAD_IDLE));
}

TEST_F(PageDownloadDiffTest, GetDiffFallback) {
  page_cloud_.status_to_return = cloud_provider::Status::NOT_SUPPORTED;

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(base_commit, "commit");
  EXPECT_THAT(changes, IsEmpty());

  EXPECT_THAT(states_, ElementsAre(DOWNLOAD_IN_PROGRESS, DOWNLOAD_IDLE));
}

TEST_F(PageDownloadDiffTest, GetDiffRetryOnNetworkError) {
  std::vector<storage::EntryChange> expected_changes;
  std::tie(page_cloud_.diff_to_return, expected_changes) = MakeTestDiff();
  storage_.remote_id_to_commit_id[encryption_service_.EncodeCommitId("base1")] = "base1";

  page_cloud_.status_to_return = cloud_provider::Status::NETWORK_ERROR;
  SetOnNewStateCallback([this] {
    if (states_.back() == DOWNLOAD_PERMANENT_ERROR) {
      QuitLoop();
    }
  });

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));

  // Allow the operation to succeed after looping through five attempts.
  RunLoopFor(kTestBackoffInterval * 4);
  page_cloud_.status_to_return = cloud_provider::Status::OK;
  RunLoopFor(kTestBackoffInterval);

  EXPECT_THAT(page_cloud_.get_diff_calls, SizeIs(6));
  EXPECT_THAT(page_cloud_.get_diff_calls,
              Each(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                  ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(base_commit, "base1");
  EXPECT_EQ(changes, expected_changes);
}

TEST_F(PageDownloadDiffTest, GetDiffNotFound) {
  page_cloud_.status_to_return = cloud_provider::Status::NOT_FOUND;

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
  EXPECT_EQ(base_commit, "");
  EXPECT_THAT(changes, IsEmpty());

  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(states_.back(), DOWNLOAD_IDLE);
}

// Tests that diffs from a base whose remote id to local id association is not locally present are
// rejected.
TEST_F(PageDownloadDiffTest, GetDiffUnknownBase) {
  std::vector<storage::EntryChange> expected_changes;
  std::tie(page_cloud_.diff_to_return, expected_changes) = MakeTestDiff();
  // We do not add the mapping from the remote commit id of base1 to base1.

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
  EXPECT_EQ(base_commit, "");
  EXPECT_THAT(changes, IsEmpty());
}

class PageCloudReturningNoDiffPack : public TestPageCloud {
 public:
  explicit PageCloudReturningNoDiffPack(fidl::InterfaceRequest<cloud_provider::PageCloud> request)
      : TestPageCloud(std::move(request)) {}
  void GetDiff(std::vector<uint8_t> commit_id, std::vector<std::vector<uint8_t>> possible_bases,
               GetDiffCallback callback) override {
    get_diff_calls.emplace_back(commit_id, possible_bases);
    callback(cloud_provider::Status::OK, {});
  }
};

TEST_F(PageDownloadDiffTest, GetDiffNoPack) {
  // Rebind the PageCloudPtr used by PageDownload.
  PageCloudReturningNoDiffPack page_cloud(page_cloud_ptr_.NewRequest());
  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
  EXPECT_EQ(base_commit, "");
  EXPECT_THAT(changes, IsEmpty());

  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(states_.back(), DOWNLOAD_IDLE);
}

TEST_P(PageDownloadDiffTest, AlteredDiffTest) {
  std::vector<storage::EntryChange> expected_changes;
  std::tie(page_cloud_.diff_to_return, expected_changes) = MakeTestDiff();
  fit::function<void(cloud_provider::Diff*)> alteration = GetParam();
  alteration(&page_cloud_.diff_to_return);

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
  EXPECT_EQ(base_commit, "");
  EXPECT_THAT(changes, IsEmpty());

  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(states_.back(), DOWNLOAD_IDLE);
}

// Only PageDownloadDiffTest.AlteredDiffTest is parametrized.
INSTANTIATE_TEST_SUITE_P(
    PageDownloadDiffTest, PageDownloadDiffTest,
    ::testing::Values(
        [](cloud_provider::Diff* diff) { diff->clear_base_state(); },
        [](cloud_provider::Diff* diff) { diff->clear_changes(); },
        [](cloud_provider::Diff* diff) { (*diff->mutable_changes())[0].clear_entry_id(); },
        [](cloud_provider::Diff* diff) { (*diff->mutable_changes())[0].set_entry_id({}); },
        [](cloud_provider::Diff* diff) { (*diff->mutable_changes())[0].clear_operation(); },
        [](cloud_provider::Diff* diff) { (*diff->mutable_changes())[0].clear_data(); },
        [](cloud_provider::Diff* diff) {
          (*diff->mutable_changes())[0].set_data(convert::ToArray("invalid"));
        }));

// Tests that diff normalization sorts changes by key, then operation.
TEST_F(PageDownloadDiffTest, NormalizationSortByKey) {
  // Create a diff with a deletion for key1, an insertion for key1 and a deletion for key2.
  std::vector<storage::EntryChange> expected_changes = {
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry2"}, true},
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::EAGER, "entry1"}, false},
      {{"key2", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry3"}, true}};
  page_cloud_.diff_to_return = MakeShuffledDiff(expected_changes);

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(base_commit, convert::ToString(storage::kFirstPageCommitId));
  // Entries are received in sorted order.
  EXPECT_EQ(changes, expected_changes);

  EXPECT_THAT(states_, ElementsAre(DOWNLOAD_IN_PROGRESS, DOWNLOAD_IDLE));
}

// Tests that the normalization removes duplicate operations.
TEST_F(PageDownloadDiffTest, NormalizationRemoveDuplicates) {
  std::vector<storage::Entry> entries = {
      {"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry0"},
      {"key1", MakeObjectIdentifier(), storage::KeyPriority::EAGER, "entry1"},
      {"key2", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry2"}};
  std::vector<storage::EntryChange> received_changes;
  std::vector<storage::EntryChange> expected_changes;
  // We have three copies of entry0: one insertion, two deletions.
  // We expect to see one deletion after normalization.
  received_changes.push_back({entries[0], false});
  received_changes.push_back({entries[0], true});
  received_changes.push_back({entries[0], true});
  expected_changes.push_back({entries[0], true});
  // We have four copies of entry1: two insertions, two deletions.
  // We expect it to not be present after normalization.
  received_changes.push_back({entries[1], false});
  received_changes.push_back({entries[1], false});
  received_changes.push_back({entries[1], true});
  received_changes.push_back({entries[1], true});
  // We have three copies of entry2: one deletion, two insertions.
  // We expect to see one insertion after normalization.
  received_changes.push_back({entries[2], false});
  received_changes.push_back({entries[2], false});
  received_changes.push_back({entries[2], true});
  expected_changes.push_back({entries[2], false});

  page_cloud_.diff_to_return = MakeShuffledDiff(received_changes);

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(base_commit, convert::ToString(storage::kFirstPageCommitId));
  // Changes are correctly simplified.
  EXPECT_EQ(changes, expected_changes);

  EXPECT_THAT(states_, ElementsAre(DOWNLOAD_IN_PROGRESS, DOWNLOAD_IDLE));
}

// Normalization should not fail on duplicate keys.
TEST_F(PageDownloadDiffTest, NormalizationDuplicateKeys) {
  // Create a diff with a three operations for key1 that don't cancel.
  std::vector<storage::EntryChange> expected_changes = {
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry2"}, true},
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::EAGER, "entry1"}, true},
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry3"}, false}};
  page_cloud_.diff_to_return = MakeShuffledDiff(expected_changes);

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  // The diff is accepted by PageDownload.
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(base_commit, convert::ToString(storage::kFirstPageCommitId));
  // Entries are received in sorted order. The order between the two deletions is undefined.
  EXPECT_THAT(changes,
              AnyOf(ElementsAre(expected_changes[0], expected_changes[1], expected_changes[2]),
                    ElementsAre(expected_changes[1], expected_changes[0], expected_changes[2])));

  EXPECT_THAT(states_, ElementsAre(DOWNLOAD_IN_PROGRESS, DOWNLOAD_IDLE));
}

// Tests that normalization fails if multiple insertions with the same entry id remain.
TEST_F(PageDownloadDiffTest, NormalizationFailsMultipleInsertions) {
  std::vector<storage::EntryChange> received_changes = {
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry1"}, false},
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry1"}, false}};
  page_cloud_.diff_to_return = MakeShuffledDiff(received_changes);

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
}

// Tests that normalization fails if multiple deletions with the same entry id remain.
TEST_F(PageDownloadDiffTest, NormalizationFailsMultipleDeletions) {
  std::vector<storage::EntryChange> received_changes = {
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry1"}, true},
      {{"key1", MakeObjectIdentifier(), storage::KeyPriority::LAZY, "entry1"}, true}};
  page_cloud_.diff_to_return = MakeShuffledDiff(received_changes);

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  EXPECT_THAT(
      page_cloud_.get_diff_calls,
      ElementsAre(Pair(ToRemoteId("commit"), std::vector<std::vector<uint8_t>>{
                                                 ToRemoteId("base1"), ToRemoteId("base2")})));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
}

class FailingDecryptEntryPayloadEncryptionService : public encryption::FakeEncryptionService {
 public:
  explicit FailingDecryptEntryPayloadEncryptionService(async_dispatcher_t* dispatcher)
      : encryption::FakeEncryptionService(dispatcher) {}

  void DecryptEntryPayload(std::string /*encrypted_data*/,
                           fit::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

using FailingDecryptEntryPayloadPageDownloadDiffTest =
    BasePageDownloadDiffTest<FailingDecryptEntryPayloadEncryptionService>;
TEST_F(FailingDecryptEntryPayloadPageDownloadDiffTest, Fail) {
  EXPECT_EQ(storage_.received_commits.size(), 0u);
  EXPECT_EQ(storage_.sync_metadata.count(convert::ToString(kTimestampKey)), 0u);

  page_cloud_.diff_to_return = MakeTestDiff().first;

  bool called;
  ledger::Status status;
  storage::CommitId base_commit;
  std::vector<storage::EntryChange> changes;
  storage_.page_sync_delegate_->GetDiff(
      "commit", {"base1", "base2"},
      ledger::Capture(ledger::SetWhenCalled(&called), &status, &base_commit, &changes));
  RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(status, ledger::Status::IO_ERROR);
  EXPECT_EQ(base_commit, "");
  EXPECT_THAT(changes, IsEmpty());

  ASSERT_FALSE(states_.empty());
  EXPECT_EQ(states_.back(), DOWNLOAD_IDLE);
}

}  // namespace
}  // namespace cloud_sync
