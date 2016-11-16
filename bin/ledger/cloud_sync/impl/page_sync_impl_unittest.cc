// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/page_sync_impl.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "apps/ledger/src/backoff/backoff.h"
#include "apps/ledger/src/cloud_provider/test/cloud_provider_empty_impl.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/test/commit_empty_impl.h"
#include "apps/ledger/src/storage/test/page_storage_empty_impl.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace cloud_sync {
namespace {

// Fake implementation of storage::Commit.
class TestCommit : public storage::test::CommitEmptyImpl {
 public:
  TestCommit() = default;
  TestCommit(storage::CommitId id, std::string content)
      : id(id), content(content) {}
  ~TestCommit() override = default;

  std::unique_ptr<Commit> Clone() const override {
    return std::make_unique<TestCommit>(id, content);
  }

  storage::CommitId GetId() const override { return id; }

  std::string GetStorageBytes() const override { return content; }

  storage::CommitId id;
  std::string content;
};

// Fake implementation of storage::PageStorage. Injects the data that PageSync
// asks about: page id, existing unsynced commits to be retrieved through
// GetUnsyncedCommitS() and new commits to be retrieved through GetCommit().
// Registers the commits marked as synced.
class TestPageStorage : public storage::test::PageStorageEmptyImpl {
 public:
  TestPageStorage() = default;
  ~TestPageStorage() override = default;

  storage::PageId GetId() override { return page_id_to_return; }

  storage::Status GetCommit(
      const storage::CommitId& commit_id,
      std::unique_ptr<const storage::Commit>* commit) override {
    if (should_fail_get_commit) {
      return storage::Status::IO_ERROR;
    }

    *commit = std::move(new_commits_to_return[commit_id]);
    new_commits_to_return.erase(commit_id);
    return storage::Status::OK;
  }

  storage::Status GetUnsyncedObjects(
      const storage::CommitId& commit_id,
      std::vector<storage::ObjectId>* object_ids) override {
    object_ids->clear();
    return storage::Status::OK;
  }

  storage::Status AddCommitWatcher(storage::CommitWatcher* watcher) override {
    return storage::Status::OK;
  }

  storage::Status RemoveCommitWatcher(
      storage::CommitWatcher* watcher) override {
    watcher_removed = true;
    return storage::Status::OK;
  }

  storage::Status GetUnsyncedCommits(
      std::vector<std::unique_ptr<const storage::Commit>>* commits) override {
    if (should_fail_get_unsynced_commits) {
      return storage::Status::IO_ERROR;
    }

    commits->swap(unsynced_commits_to_return);
    unsynced_commits_to_return.clear();
    return storage::Status::OK;
  }

  storage::Status MarkCommitSynced(
      const storage::CommitId& commit_id) override {
    commits_marked_as_synced.insert(commit_id);
    return storage::Status::OK;
  }

  storage::PageId page_id_to_return;
  // Commits to be returned from GetUnsyncedCommits calls.
  std::vector<std::unique_ptr<const storage::Commit>>
      unsynced_commits_to_return;
  // Commits to be returned from GetCommit() calls.
  std::unordered_map<storage::CommitId, std::unique_ptr<const storage::Commit>>
      new_commits_to_return;
  bool should_fail_get_unsynced_commits = false;
  bool should_fail_get_commit = false;

  std::set<storage::CommitId> commits_marked_as_synced;
  bool watcher_removed = false;
};

// Fake implementation of cloud_provider::CloudProvider. Injects the returned
// status for commit notification upload, allowing the test to make them fail.
// Registers for inspection the notifications passed by PageSync.
class TestCloudProvider : public cloud_provider::test::CloudProviderEmptyImpl {
 public:
  TestCloudProvider(mtl::MessageLoop* message_loop)
      : message_loop_(message_loop) {}

  ~TestCloudProvider() override = default;

  void AddCommit(
      const cloud_provider::Commit& commit,
      const std::function<void(cloud_provider::Status)>& callback) override {
    received_commits.push_back(commit.Clone());
    message_loop_->task_runner()->PostTask(
        [this, callback]() { callback(commit_status_to_return); });
  }

  cloud_provider::Status commit_status_to_return = cloud_provider::Status::OK;
  std::vector<cloud_provider::Commit> received_commits;

 private:
  mtl::MessageLoop* message_loop_;
};

// Dummy implementation of a backoff policy, which always returns zero backoff
// time..
class TestBackoff : public backoff::Backoff {
 public:
  TestBackoff(int* get_next_count) : get_next_count_(get_next_count) {}
  ~TestBackoff() override {}

  ftl::TimeDelta GetNext() override {
    (*get_next_count_)++;
    return ftl::TimeDelta::FromSeconds(0);
  }

  void Reset() override {}

  int* get_next_count_;
};

class PageSyncImplTest : public ::testing::Test {
 public:
  PageSyncImplTest()
      : cloud_provider_(&message_loop_),
        page_sync_(message_loop_.task_runner(),
                   &storage_,
                   &cloud_provider_,
                   std::make_unique<TestBackoff>(&backoff_get_next_calls_),
                   [this] {
                     EXPECT_FALSE(error_callback_called_);
                     error_callback_called_ = true;
                   }) {}
  ~PageSyncImplTest() override {}

  // ::testing::Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    // Prevent a failing test from hanging forever, as in some of the tests we
    // only quit the message loop when the condition being tested becomes true.
    message_loop_.task_runner()->PostDelayedTask(
        [this] {
          FTL_LOG(WARNING) << "Quitting a slow to finish test.";
          message_loop_.QuitNow();
        },
        ftl::TimeDelta::FromSeconds(1));
  }

 protected:
  mtl::MessageLoop message_loop_;
  TestPageStorage storage_;
  TestCloudProvider cloud_provider_;
  int backoff_get_next_calls_ = 0;
  PageSyncImpl page_sync_;
  bool error_callback_called_ = false;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageSyncImplTest);
};

// Verifies that the backlog of commits to upload returned from
// GetUnsyncedCommits() is uploaded to CloudProvider.
TEST_F(PageSyncImplTest, UploadExistingCommits) {
  storage_.unsynced_commits_to_return.push_back(
      std::make_unique<const TestCommit>("id1", "content1"));
  storage_.unsynced_commits_to_return.push_back(
      std::make_unique<const TestCommit>("id2", "content2"));
  page_sync_.Start();

  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  message_loop_.Run();

  EXPECT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id1", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id2", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content2", cloud_provider_.received_commits[1].content);
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id2"));
}

// Verfies that the new commits that PageSync is notified about through storage
// watcher are uploaded to CloudProvider, with the exception of commits that
// themselves come from sync.
TEST_F(PageSyncImplTest, UploadNewCommits) {
  page_sync_.Start();
  storage_.new_commits_to_return["id1"] =
      std::make_unique<const TestCommit>("id1", "content1");
  page_sync_.OnNewCommit(TestCommit("id1", "content1"),
                         storage::ChangeSource::LOCAL);

  // The commit coming from sync should be ignored.
  storage_.new_commits_to_return["id2"] =
      std::make_unique<const TestCommit>("id2", "content2");
  page_sync_.OnNewCommit(TestCommit("id2", "content2"),
                         storage::ChangeSource::SYNC);

  storage_.new_commits_to_return["id3"] =
      std::make_unique<const TestCommit>("id3", "content3");
  page_sync_.OnNewCommit(TestCommit("id3", "content3"),
                         storage::ChangeSource::LOCAL);

  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  message_loop_.Run();

  EXPECT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id1", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id3", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content3", cloud_provider_.received_commits[1].content);
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id3"));
}

// Verifies that existing commits are uploaded before the new ones.
TEST_F(PageSyncImplTest, UploadExistingAndNewCommits) {
  storage_.unsynced_commits_to_return.push_back(
      std::make_unique<const TestCommit>("id1", "content1"));
  page_sync_.Start();

  storage_.new_commits_to_return["id2"] =
      std::make_unique<const TestCommit>("id2", "content2");
  page_sync_.OnNewCommit(TestCommit("id2", "content2"),
                         storage::ChangeSource::LOCAL);

  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  message_loop_.Run();

  EXPECT_EQ(2u, cloud_provider_.received_commits.size());
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
TEST_F(PageSyncImplTest, RecoverableError) {
  storage_.unsynced_commits_to_return.push_back(
      std::make_unique<const TestCommit>("id1", "content1"));
  cloud_provider_.commit_status_to_return =
      cloud_provider::Status::UNKNOWN_ERROR;
  page_sync_.Start();

  // Test cloud provider logs every commit, even if it reports that upload
  // failed for each. Here we loop through five attempts to upload the commit.
  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 5u) {
      message_loop_.PostQuitTask();
    }
  });
  message_loop_.Run();

  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_EQ(5, backoff_get_next_calls_);
}

// Verifies that if listing the original commits to be uploaded fails, the
// client is notified about the error and the storage watcher is removed, so
// that subsequent commits are not handled. (as this would violate the contract
// of uploading commits in order)
TEST_F(PageSyncImplTest, FailToListCommits) {
  EXPECT_FALSE(storage_.watcher_removed);
  EXPECT_FALSE(error_callback_called_);
  storage_.should_fail_get_unsynced_commits = true;
  page_sync_.Start();
  EXPECT_TRUE(error_callback_called_);
  EXPECT_TRUE(storage_.watcher_removed);
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
}

}  // namespace
}  // namespace cloud_sync
