// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_download.h"

#include <map>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/cloud_sync/impl/constants.h"
#include "peridot/bin/ledger/storage/test/page_storage_empty_impl.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace cloud_sync {

namespace {

// Fake implementation of storage::PageStorage. Injects the data that
// CommitUpload asks about: page id and unsynced objects to be uploaded.
// Registers the reported results of the upload: commits and objects marked as
// synced.
class TestPageStorage : public storage::test::PageStorageEmptyImpl {
 public:
  explicit TestPageStorage(fsl::MessageLoop* message_loop)
      : message_loop_(message_loop) {}

  void AddCommitsFromSync(
      std::vector<storage::PageStorage::CommitIdAndBytes> ids_and_bytes,
      std::function<void(storage::Status status)> callback) override {
    if (should_fail_add_commit_from_sync) {
      message_loop_->task_runner()->PostTask(
          [callback]() { callback(storage::Status::IO_ERROR); });
      return;
    }
    message_loop_->task_runner()->PostTask(fxl::MakeCopyable(
        [ this, ids_and_bytes = std::move(ids_and_bytes), callback ]() mutable {
          for (auto& commit : ids_and_bytes) {
            received_commits[std::move(commit.id)] = std::move(commit.bytes);
          }
          callback(storage::Status::OK);
        }));
  }

  void SetSyncMetadata(fxl::StringView key,
                       fxl::StringView value,
                       std::function<void(storage::Status)> callback) override {
    sync_metadata[key.ToString()] = value.ToString();
    message_loop_->task_runner()->PostTask(
        [callback]() { callback(storage::Status::OK); });
  }

  bool should_fail_add_commit_from_sync = false;
  std::map<storage::CommitId, std::string> received_commits;
  std::map<std::string, std::string> sync_metadata;

 private:
  fsl::MessageLoop* message_loop_;
};

class BatchDownloadTest : public test::TestWithMessageLoop {
 public:
  BatchDownloadTest() : storage_(&message_loop_) {}
  ~BatchDownloadTest() override {}

 protected:
  TestPageStorage storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BatchDownloadTest);
};

TEST_F(BatchDownloadTest, AddCommit) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider_firebase::Record> records;
  records.emplace_back(cloud_provider_firebase::Commit("id1", "content1"),
                       "42");
  BatchDownload batch_download(&storage_, std::move(records),
                               [this, &done_calls] {
                                 done_calls++;
                                 message_loop_.PostQuitTask();
                               },
                               [&error_calls] { error_calls++; });
  batch_download.Start();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, done_calls);
  EXPECT_EQ(0, error_calls);
  EXPECT_EQ(1u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("42", storage_.sync_metadata[kTimestampKey.ToString()]);
}

TEST_F(BatchDownloadTest, AddMultipleCommits) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider_firebase::Record> records;
  records.emplace_back(cloud_provider_firebase::Commit("id1", "content1"),
                       "42");
  records.emplace_back(cloud_provider_firebase::Commit("id2", "content2"),
                       "43");
  BatchDownload batch_download(&storage_, std::move(records),
                               [this, &done_calls] {
                                 done_calls++;
                                 message_loop_.PostQuitTask();
                               },
                               [&error_calls] { error_calls++; });
  batch_download.Start();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, done_calls);
  EXPECT_EQ(0, error_calls);
  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata[kTimestampKey.ToString()]);
}

TEST_F(BatchDownloadTest, FailToAddCommit) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider_firebase::Record> records;
  records.emplace_back(cloud_provider_firebase::Commit("id1", "content1"),
                       "42");
  BatchDownload batch_download(&storage_, std::move(records),
                               [&done_calls] { done_calls++; },
                               [this, &error_calls] {
                                 error_calls++;
                                 message_loop_.PostQuitTask();
                               });
  storage_.should_fail_add_commit_from_sync = true;
  batch_download.Start();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0, done_calls);
  EXPECT_EQ(1, error_calls);
  EXPECT_TRUE(storage_.received_commits.empty());
  EXPECT_EQ(0u, storage_.sync_metadata.count(kTimestampKey.ToString()));
}

}  // namespace

}  // namespace cloud_sync
