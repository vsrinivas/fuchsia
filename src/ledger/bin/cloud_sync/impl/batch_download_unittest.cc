// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/batch_download.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <map>

#include "gtest/gtest.h"
#include "src/ledger/bin/cloud_sync/impl/constants.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/lib/callback/capture.h"

namespace cloud_sync {

namespace {

// Creates a dummy continuation token.
std::unique_ptr<cloud_provider::PositionToken> MakeToken(convert::ExtendedStringView token_id) {
  auto token = std::make_unique<cloud_provider::PositionToken>();
  token->opaque_id = convert::ToArray(token_id);
  return token;
}

// Fake implementation of storage::PageStorage. Injects the data that
// CommitUpload asks about: page id and unsynced objects to be uploaded.
// Registers the reported results of the upload: commits and objects marked as
// synced.
class TestPageStorage : public storage::PageStorageEmptyImpl {
 public:
  explicit TestPageStorage(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void AddCommitsFromSync(std::vector<storage::PageStorage::CommitIdAndBytes> ids_and_bytes,
                          storage::ChangeSource source,
                          fit::function<void(ledger::Status)> callback) override {
    ASSERT_EQ(source, storage::ChangeSource::CLOUD);
    if (should_fail_add_commit_from_sync) {
      async::PostTask(dispatcher_,
                      [callback = std::move(callback)]() { callback(ledger::Status::IO_ERROR); });
      return;
    }
    async::PostTask(dispatcher_, [this, ids_and_bytes = std::move(ids_and_bytes),
                                  callback = std::move(callback)]() mutable {
      for (auto& commit : ids_and_bytes) {
        received_commits[std::move(commit.id)] = std::move(commit.bytes);
      }
      callback(ledger::Status::OK);
    });
  }

  void SetSyncMetadata(fxl::StringView key, fxl::StringView value,
                       fit::function<void(ledger::Status)> callback) override {
    sync_metadata[key.ToString()] = value.ToString();
    async::PostTask(dispatcher_,
                    [callback = std::move(callback)]() { callback(ledger::Status::OK); });
  }

  bool should_fail_add_commit_from_sync = false;
  std::map<storage::CommitId, std::string> received_commits;
  std::map<std::string, std::string> sync_metadata;

 private:
  async_dispatcher_t* const dispatcher_;
};

class BatchDownloadTest : public gtest::TestLoopFixture {
 public:
  BatchDownloadTest() : storage_(dispatcher()), encryption_service_(dispatcher()) {}
  BatchDownloadTest(const BatchDownloadTest&) = delete;
  BatchDownloadTest& operator=(const BatchDownloadTest&) = delete;
  ~BatchDownloadTest() override = default;

 protected:
  TestPageStorage storage_;
  encryption::FakeEncryptionService encryption_service_;
};

TEST_F(BatchDownloadTest, AddCommit) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Commit> entries;
  std::string value = "content1";
  storage::CommitId id = storage::ComputeCommitId(value);
  entries.push_back(MakeTestCommit(&encryption_service_, value));
  BatchDownload batch_download(
      &storage_, &encryption_service_, std::move(entries), MakeToken("42"),
      [&done_calls] { done_calls++; }, [&error_calls] { error_calls++; });
  batch_download.Start();

  RunLoopUntilIdle();
  EXPECT_EQ(done_calls, 1);
  EXPECT_EQ(error_calls, 0);
  EXPECT_EQ(storage_.received_commits.size(), 1u);
  EXPECT_EQ(storage_.received_commits[id], value);
  EXPECT_EQ(storage_.sync_metadata[kTimestampKey.ToString()], "42");
}

TEST_F(BatchDownloadTest, AddMultipleCommits) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Commit> entries;
  std::string value1 = "content1";
  storage::CommitId id1 = storage::ComputeCommitId(value1);
  entries.push_back(MakeTestCommit(&encryption_service_, value1));
  std::string value2 = "content2";
  storage::CommitId id2 = storage::ComputeCommitId(value2);
  entries.push_back(MakeTestCommit(&encryption_service_, value2));
  BatchDownload batch_download(
      &storage_, &encryption_service_, std::move(entries), MakeToken("43"),
      [&done_calls] { done_calls++; }, [&error_calls] { error_calls++; });
  batch_download.Start();

  RunLoopUntilIdle();
  EXPECT_EQ(done_calls, 1);
  EXPECT_EQ(error_calls, 0);
  EXPECT_EQ(storage_.received_commits.size(), 2u);
  EXPECT_EQ(storage_.received_commits[id1], value1);
  EXPECT_EQ(storage_.received_commits[id2], value2);
  EXPECT_EQ(storage_.sync_metadata[kTimestampKey.ToString()], "43");
}

TEST_F(BatchDownloadTest, FailToAddCommit) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Commit> entries;
  entries.push_back(MakeTestCommit(&encryption_service_, "content1"));
  BatchDownload batch_download(
      &storage_, &encryption_service_, std::move(entries), MakeToken("42"),
      [&done_calls] { done_calls++; }, [&error_calls] { error_calls++; });
  storage_.should_fail_add_commit_from_sync = true;
  batch_download.Start();

  RunLoopUntilIdle();
  EXPECT_EQ(done_calls, 0);
  EXPECT_EQ(error_calls, 1);
  EXPECT_TRUE(storage_.received_commits.empty());
  EXPECT_EQ(storage_.sync_metadata.count(kTimestampKey.ToString()), 0u);
}

TEST_F(BatchDownloadTest, MissingId) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Commit> entries;
  // Upload a commit without id.
  cloud_provider::Commit commit = MakeTestCommit(&encryption_service_, "content1");
  commit.clear_id();
  entries.push_back(std::move(commit));
  BatchDownload batch_download(
      &storage_, &encryption_service_, std::move(entries), MakeToken("42"),
      [&done_calls] { done_calls++; }, [&error_calls] { error_calls++; });
  batch_download.Start();

  RunLoopUntilIdle();
  EXPECT_EQ(done_calls, 0);
  EXPECT_EQ(error_calls, 1);
  EXPECT_TRUE(storage_.received_commits.empty());
  EXPECT_EQ(storage_.sync_metadata.count(kTimestampKey.ToString()), 0u);
}

TEST_F(BatchDownloadTest, MissingData) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Commit> entries;
  // Upload a commit without data.
  cloud_provider::Commit commit = MakeTestCommit(&encryption_service_, "content1");
  commit.clear_data();
  entries.push_back(std::move(commit));
  BatchDownload batch_download(
      &storage_, &encryption_service_, std::move(entries), MakeToken("42"),
      [&done_calls] { done_calls++; }, [&error_calls] { error_calls++; });
  batch_download.Start();

  RunLoopUntilIdle();
  EXPECT_EQ(done_calls, 0);
  EXPECT_EQ(error_calls, 1);
  EXPECT_TRUE(storage_.received_commits.empty());
  EXPECT_EQ(storage_.sync_metadata.count(kTimestampKey.ToString()), 0u);
}

TEST_F(BatchDownloadTest, IdMismatch) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Commit> entries;
  // Upload a commit with an id which is not an encoded hash of the content.
  cloud_provider::Commit commit = MakeTestCommit(&encryption_service_, "content1");
  commit.set_id(convert::ToArray("id1"));
  entries.push_back(std::move(commit));
  BatchDownload batch_download(
      &storage_, &encryption_service_, std::move(entries), MakeToken("42"),
      [&done_calls] { done_calls++; }, [&error_calls] { error_calls++; });
  batch_download.Start();

  RunLoopUntilIdle();
  EXPECT_EQ(done_calls, 0);
  EXPECT_EQ(error_calls, 1);
  EXPECT_TRUE(storage_.received_commits.empty());
  EXPECT_EQ(storage_.sync_metadata.count(kTimestampKey.ToString()), 0u);
}

}  // namespace

}  // namespace cloud_sync
