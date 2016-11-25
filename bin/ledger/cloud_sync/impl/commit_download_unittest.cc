// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/commit_download.h"

#include <unordered_map>

#include "apps/ledger/src/storage/test/page_storage_empty_impl.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace cloud_sync {

namespace {

// Fake implementation of storage::PageStorage. Injects the data that
// CommitUpload asks about: page id and unsynced objects to be uploaded.
// Registers the reported results of the upload: commits and objects marked as
// synced.
class TestPageStorage : public storage::test::PageStorageEmptyImpl {
 public:
  TestPageStorage(mtl::MessageLoop* message_loop)
      : message_loop_(message_loop) {}

  void AddCommitsFromSync(
      std::vector<storage::PageStorage::CommitIdAndBytes> ids_and_bytes,
      std::function<void(storage::Status status)> callback) override {
    if (should_fail_add_commit_from_sync) {
      message_loop_->task_runner()->PostTask(
          [this, callback]() { callback(storage::Status::IO_ERROR); });
      return;
    }
    message_loop_->task_runner()->PostTask(ftl::MakeCopyable(
        [ this, ids_and_bytes = std::move(ids_and_bytes), callback ]() {
          for (auto& commit : ids_and_bytes) {
            received_commits[std::move(commit.id)] = std::move(commit.bytes);
          }
          callback(storage::Status::OK);
        }));
  }

  storage::Status SetSyncMetadata(ftl::StringView sync_state) override {
    sync_metadata = sync_state.ToString();
    return storage::Status::OK;
  }

  bool should_fail_add_commit_from_sync = false;
  std::unordered_map<storage::CommitId, std::string> received_commits;
  std::string sync_metadata;

 private:
  mtl::MessageLoop* message_loop_;
};

class CommitDownloadTest : public test::TestWithMessageLoop {
 public:
  CommitDownloadTest() : storage_(&message_loop_) {}
  ~CommitDownloadTest() override {}

 protected:
  TestPageStorage storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CommitDownloadTest);
};

TEST_F(CommitDownloadTest, AddCommit) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Record> records;
  records.emplace_back(cloud_provider::Commit("id1", "content1", {}), "42");
  CommitDownload commit_download(
      &storage_, std::move(records),
      [this, &done_calls] {
        done_calls++;
        message_loop_.PostQuitTask();
      },
      [this, &error_calls] { error_calls++; });
  commit_download.Start();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, done_calls);
  EXPECT_EQ(0, error_calls);
  EXPECT_EQ(1u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("42", storage_.sync_metadata);
}

TEST_F(CommitDownloadTest, AddMultipleCommit) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Record> records;
  records.emplace_back(cloud_provider::Commit("id1", "content1", {}), "42");
  records.emplace_back(cloud_provider::Commit("id2", "content2", {}), "43");
  CommitDownload commit_download(
      &storage_, std::move(records),
      [this, &done_calls] {
        done_calls++;
        message_loop_.PostQuitTask();
      },
      [this, &error_calls] { error_calls++; });
  commit_download.Start();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, done_calls);
  EXPECT_EQ(0, error_calls);
  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata);
}

TEST_F(CommitDownloadTest, FailToAddCommit) {
  int done_calls = 0;
  int error_calls = 0;
  std::vector<cloud_provider::Record> records;
  records.emplace_back(cloud_provider::Commit("id1", "content1", {}), "42");
  CommitDownload commit_download(
      &storage_, std::move(records),
      [this, &done_calls] { done_calls++; },
      [this, &error_calls] {
        error_calls++;
        message_loop_.PostQuitTask();
      });
  storage_.should_fail_add_commit_from_sync = true;
  commit_download.Start();

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0, done_calls);
  EXPECT_EQ(1, error_calls);
  EXPECT_TRUE(storage_.received_commits.empty());
  EXPECT_EQ("", storage_.sync_metadata);
}

}  // namespace

}  // namespace cloud_sync
