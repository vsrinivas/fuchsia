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
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"
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

  static std::vector<std::unique_ptr<const Commit>> AsList(
      storage::CommitId id,
      std::string content) {
    std::vector<std::unique_ptr<const Commit>> result;
    result.push_back(std::make_unique<TestCommit>(id, content));
    return result;
  }

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
  TestPageStorage(mtl::MessageLoop* message_loop)
      : message_loop_(message_loop) {}

  storage::PageId GetId() override { return page_id_to_return; }

  void SetSyncDelegate(storage::PageSyncDelegate* page_sync) override {}

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

  void AddCommitsFromSync(
      std::vector<PageStorage::CommitIdAndBytes> ids_and_bytes,
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

  storage::Status GetUnsyncedObjects(
      const storage::CommitId& commit_id,
      std::vector<storage::ObjectId>* object_ids) override {
    object_ids->clear();
    return storage::Status::OK;
  }

  storage::Status AddCommitWatcher(storage::CommitWatcher* watcher) override {
    watcher_set = true;
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

  storage::Status SetSyncMetadata(ftl::StringView sync_state) override {
    sync_metadata = sync_state.ToString();
    return storage::Status::OK;
  }

  storage::Status GetSyncMetadata(std::string* sync_state) override {
    *sync_state = sync_metadata;
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
  bool should_fail_add_commit_from_sync = false;

  std::set<storage::CommitId> commits_marked_as_synced;
  bool watcher_set = false;
  bool watcher_removed = false;
  std::unordered_map<storage::CommitId, std::string> received_commits;
  std::string sync_metadata;

 private:
  mtl::MessageLoop* message_loop_;
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

  void WatchCommits(const std::string& min_timestamp,
                    cloud_provider::CommitWatcher* watcher) override {
    for (auto& record : notifications_to_deliver) {
      message_loop_->task_runner()->PostTask(ftl::MakeCopyable([
        watcher, commit = std::move(record.commit),
        timestamp = std::move(record.timestamp)
      ]() mutable {
        watcher->OnRemoteCommit(std::move(commit), std::move(timestamp));
      }));
    }
  }

  void UnwatchCommits(cloud_provider::CommitWatcher* watcher) override {
    watcher_removed = true;
  }

  void GetCommits(const std::string& min_timestamp,
                  std::function<void(cloud_provider::Status,
                                     std::vector<cloud_provider::Record>)>
                      callback) override {
    get_commits_calls++;
    if (should_fail_get_commits) {
      message_loop_->task_runner()->PostTask([this, callback]() {
        callback(cloud_provider::Status::NETWORK_ERROR, {});
      });
      return;
    }

    message_loop_->task_runner()->PostTask([this, callback]() {
      callback(cloud_provider::Status::OK, std::move(records_to_return));
    });
  }

  bool should_fail_get_commits = false;
  std::vector<cloud_provider::Record> records_to_return;
  std::vector<cloud_provider::Record> notifications_to_deliver;
  cloud_provider::Status commit_status_to_return = cloud_provider::Status::OK;

  unsigned int get_commits_calls = 0u;
  std::vector<cloud_provider::Commit> received_commits;
  bool watcher_removed = false;

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

class PageSyncImplTest : public test::TestWithMessageLoop {
 public:
  PageSyncImplTest()
      : storage_(&message_loop_),
        cloud_provider_(&message_loop_),
        page_sync_(message_loop_.task_runner(),
                   &storage_,
                   &cloud_provider_,
                   std::make_unique<TestBackoff>(&backoff_get_next_calls_),
                   [this] {
                     EXPECT_FALSE(error_callback_called_);
                     error_callback_called_ = true;
                   }) {}
  ~PageSyncImplTest() override {}

 protected:
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
TEST_F(PageSyncImplTest, UploadBacklog) {
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
  EXPECT_FALSE(RunLoopWithTimeout());

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
  page_sync_.OnNewCommits(TestCommit::AsList("id1", "content1"),
                          storage::ChangeSource::LOCAL);

  // The commit coming from sync should be ignored.
  storage_.new_commits_to_return["id2"] =
      std::make_unique<const TestCommit>("id2", "content2");
  page_sync_.OnNewCommits(TestCommit::AsList("id2", "content2"),
                          storage::ChangeSource::SYNC);

  storage_.new_commits_to_return["id3"] =
      std::make_unique<const TestCommit>("id3", "content3");
  page_sync_.OnNewCommits(TestCommit::AsList("id3", "content3"),
                          storage::ChangeSource::LOCAL);

  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

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
  page_sync_.OnNewCommits(TestCommit::AsList("id2", "content2"),
                          storage::ChangeSource::LOCAL);

  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

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
TEST_F(PageSyncImplTest, RetryUpload) {
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
  EXPECT_FALSE(RunLoopWithTimeout());

  // Verify that the commit is still not marked as synced in storage.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_EQ(5, backoff_get_next_calls_);
}

// Verifies that the on idle callback is called when there is no pending upload
// tasks.
TEST_F(PageSyncImplTest, UploadIdleCallback) {
  int on_idle_calls = 0;

  storage_.unsynced_commits_to_return.push_back(
      std::make_unique<const TestCommit>("id1", "content1"));
  storage_.unsynced_commits_to_return.push_back(
      std::make_unique<const TestCommit>("id2", "content2"));

  page_sync_.SetOnIdle([&on_idle_calls] { on_idle_calls++; });
  page_sync_.Start();

  // Stop the message loop when the cloud receives the last commit (before
  // cloud sync receives the async confirmation), and verify that the idle
  // callback is not yet called.
  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 2u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0, on_idle_calls);
  EXPECT_FALSE(page_sync_.IsIdle());

  // Let the confirmation be delivered and verify that the idle callback was
  // called.
  message_loop_.PostQuitTask();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_sync_.IsIdle());

  // Notify about a new commit to upload and verify that the idle callback was
  // called again on completion.
  storage_.new_commits_to_return["id3"] =
      std::make_unique<const TestCommit>("id3", "content3");
  page_sync_.OnNewCommits(TestCommit::AsList("id3", "content3"),
                          storage::ChangeSource::LOCAL);
  EXPECT_FALSE(page_sync_.IsIdle());
  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.received_commits.size() == 3u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_FALSE(page_sync_.IsIdle());

  message_loop_.PostQuitTask();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_sync_.IsIdle());
}

// Verifies that if listing the original commits to be uploaded fails, the
// client is notified about the error and the storage watcher is never set, so
// that subsequent commits are not handled. (as this would violate the contract
// of uploading commits in order)
TEST_F(PageSyncImplTest, FailToListCommits) {
  EXPECT_FALSE(storage_.watcher_set);
  EXPECT_FALSE(error_callback_called_);
  storage_.should_fail_get_unsynced_commits = true;
  page_sync_.Start();
  EXPECT_TRUE(error_callback_called_);
  EXPECT_FALSE(storage_.watcher_set);
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
}

// Verifies that the backlog of unsynced commits is retrieved from the cloud
// provider and saved in storage.
TEST_F(PageSyncImplTest, DownloadBacklog) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ("", storage_.sync_metadata);

  cloud_provider_.records_to_return.push_back(cloud_provider::Record(
      cloud_provider::Commit("id1", "content1", {}), "42"));
  cloud_provider_.records_to_return.push_back(cloud_provider::Record(
      cloud_provider::Commit("id2", "content2", {}), "43"));

  int on_backlog_downloaded_calls = 0;
  page_sync_.SetOnBacklogDownloaded(
      [&on_backlog_downloaded_calls] { on_backlog_downloaded_calls++; });
  page_sync_.Start();

  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() != 0u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata);
  EXPECT_EQ(1, on_backlog_downloaded_calls);
}

// Verifies that callbacks are correctly run after downloading an empty backlog
// of remote commits.
TEST_F(PageSyncImplTest, DownloadEmptyBacklog) {
  int on_backlog_downloaded_calls = 0;
  int on_idle_calls = 0;
  page_sync_.SetOnBacklogDownloaded(
      [&on_backlog_downloaded_calls] { on_backlog_downloaded_calls++; });
  page_sync_.SetOnIdle([this, &on_idle_calls] {
    on_idle_calls++;
    message_loop_.PostQuitTask();
  });
  page_sync_.Start();
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_backlog_downloaded_calls);
  EXPECT_EQ(1, on_idle_calls);
}

// Verifies that commit notifications about new commits in cloud provider are
// received and passed to storage.
TEST_F(PageSyncImplTest, ReceiveNotifications) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ("", storage_.sync_metadata);

  cloud_provider_.notifications_to_deliver.push_back(cloud_provider::Record(
      cloud_provider::Commit("id1", "content1", {}), "42"));
  cloud_provider_.notifications_to_deliver.push_back(cloud_provider::Record(
      cloud_provider::Commit("id2", "content2", {}), "43"));
  page_sync_.Start();

  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata);
}

// Verify that the backlog commits are downloaded before receiving notifications
// about the new ones.
TEST_F(PageSyncImplTest, DownloadBacklogThenReceiveNotifications) {
  EXPECT_EQ(0u, storage_.received_commits.size());
  EXPECT_EQ("", storage_.sync_metadata);

  cloud_provider_.records_to_return.push_back(cloud_provider::Record(
      cloud_provider::Commit("id1", "content1", {}), "42"));
  cloud_provider_.notifications_to_deliver.push_back(cloud_provider::Record(
      cloud_provider::Commit("id2", "content2", {}), "43"));
  page_sync_.Start();

  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 1u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("42", storage_.sync_metadata);

  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 2u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(2u, storage_.received_commits.size());
  EXPECT_EQ("content2", storage_.received_commits["id2"]);
  EXPECT_EQ("43", storage_.sync_metadata);
}

// Verifies that failing attempts to download the backlog of unsynced commits
// are retried.
TEST_F(PageSyncImplTest, RetryDownloadBacklog) {
  cloud_provider_.records_to_return.push_back(cloud_provider::Record(
      cloud_provider::Commit("id1", "content1", {}), "42"));
  cloud_provider_.should_fail_get_commits = true;
  page_sync_.Start();

  // Loop through five attempts to download the backlog.
  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.get_commits_calls == 5u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, storage_.received_commits.size());

  cloud_provider_.should_fail_get_commits = false;
  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 1u) {
      message_loop_.QuitNow();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, storage_.received_commits.size());
  EXPECT_EQ("content1", storage_.received_commits["id1"]);
  EXPECT_EQ("42", storage_.sync_metadata);
}

// Verifies that a failure to persist the remote commit stops syncing remote
// commits and calls the error callback.
TEST_F(PageSyncImplTest, FailToStoreRemoteCommit) {
  EXPECT_FALSE(cloud_provider_.watcher_removed);
  EXPECT_FALSE(error_callback_called_);

  cloud_provider_.notifications_to_deliver.push_back(cloud_provider::Record(
      cloud_provider::Commit("id1", "content1", {}), "42"));
  storage_.should_fail_add_commit_from_sync = true;
  page_sync_.Start();

  message_loop_.SetAfterTaskCallback([this] {
    if (cloud_provider_.watcher_removed) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(cloud_provider_.watcher_removed);
  EXPECT_TRUE(error_callback_called_);
}

// Verifies that the on idle callback is called when there is no download in
// progress.
TEST_F(PageSyncImplTest, DownloadIdleCallback) {
  cloud_provider_.records_to_return.push_back(cloud_provider::Record(
      cloud_provider::Commit("id1", "content1", {}), "42"));
  cloud_provider_.records_to_return.push_back(cloud_provider::Record(
      cloud_provider::Commit("id2", "content2", {}), "43"));

  int on_idle_calls = 0;
  page_sync_.SetOnIdle([&on_idle_calls] { on_idle_calls++; });
  page_sync_.Start();
  EXPECT_EQ(0, on_idle_calls);
  EXPECT_FALSE(page_sync_.IsIdle());

  // Run the message loop and verify that the sync is idle after all remote
  // commits are added to storage.
  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 2u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1, on_idle_calls);
  EXPECT_TRUE(page_sync_.IsIdle());

  // Notify about a new commit to download and verify that the idle callback was
  // called again on completion.
  page_sync_.OnRemoteCommit(cloud_provider::Commit("id3", "content3", {}),
                            "44");
  EXPECT_FALSE(page_sync_.IsIdle());
  message_loop_.SetAfterTaskCallback([this] {
    if (storage_.received_commits.size() == 3u) {
      message_loop_.PostQuitTask();
    }
  });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(2, on_idle_calls);
  EXPECT_TRUE(page_sync_.IsIdle());
}

}  // namespace
}  // namespace cloud_sync
