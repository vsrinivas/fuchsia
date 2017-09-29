// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_upload.h"

#include <functional>
#include <map>
#include <utility>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/auth_provider/test/test_auth_provider.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/cloud_provider/public/page_cloud_handler.h"
#include "peridot/bin/ledger/cloud_provider/test/page_cloud_handler_empty_impl.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/test/commit_empty_impl.h"
#include "peridot/bin/ledger/storage/test/page_storage_empty_impl.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace cloud_sync {
namespace {

// Fake implementation of storage::Commit.
class TestCommit : public storage::test::CommitEmptyImpl {
 public:
  TestCommit(std::string id, std::string storage_bytes)
      : id(std::move(id)), storage_bytes(std::move(storage_bytes)) {}
  ~TestCommit() override = default;

  const storage::CommitId& GetId() const override { return id; }

  fxl::StringView GetStorageBytes() const override { return storage_bytes; }

  std::unique_ptr<storage::Commit> Clone() const override {
    return std::make_unique<TestCommit>(id, storage_bytes);
  }

  storage::CommitId id;
  std::string storage_bytes;
};

// Fake implementation of storage::Object.
class TestObject : public storage::Object {
 public:
  TestObject() = default;
  TestObject(storage::ObjectId id, std::string data)
      : id(std::move(id)), data(std::move(data)) {}
  ~TestObject() override = default;

  storage::ObjectId GetId() const override { return id; };

  storage::Status GetData(fxl::StringView* result) const override {
    *result = fxl::StringView(data);
    return storage::Status::OK;
  }

  storage::ObjectId id;
  std::string data;
};

// Fake implementation of storage::PageStorage. Injects the data that
// BatchUpload asks about: page id and unsynced objects to be uploaded.
// Registers the reported results of the upload: commits and objects marked as
// synced.
class TestPageStorage : public storage::test::PageStorageEmptyImpl {
 public:
  TestPageStorage() = default;
  ~TestPageStorage() override = default;

  void GetUnsyncedCommits(
      std::function<void(storage::Status,
                         std::vector<std::unique_ptr<const storage::Commit>>)>
          callback) override {
    std::vector<std::unique_ptr<const storage::Commit>> results;
    std::transform(unsynced_commits.begin(), unsynced_commits.end(),
                   std::inserter(results, results.begin()),
                   [](const std::unique_ptr<const storage::Commit>& commit) {
                     return commit->Clone();
                   });
    callback(storage::Status::OK, std::move(results));
  }

  void GetUnsyncedPieces(
      std::function<void(storage::Status, std::vector<storage::ObjectId>)>
          callback) override {
    std::vector<storage::ObjectId> object_ids;
    for (auto& id_object_pair : unsynced_objects_to_return) {
      object_ids.push_back(id_object_pair.first);
    }
    callback(storage::Status::OK, std::move(object_ids));
  }

  void GetObject(storage::ObjectIdView object_id,
                 Location /*location*/,
                 std::function<void(storage::Status,
                                    std::unique_ptr<const storage::Object>)>
                     callback) override {
    GetPiece(object_id, callback);
  }

  void GetPiece(storage::ObjectIdView object_id,
                std::function<void(storage::Status,
                                   std::unique_ptr<const storage::Object>)>
                    callback) override {
    callback(storage::Status::OK,
             std::move(unsynced_objects_to_return[object_id.ToString()]));
  }

  void MarkPieceSynced(storage::ObjectIdView object_id,
                       std::function<void(storage::Status)> callback) override {
    objects_marked_as_synced.insert(object_id.ToString());
    callback(storage::Status::OK);
  }

  void MarkCommitSynced(
      const storage::CommitId& commit_id,
      std::function<void(storage::Status)> callback) override {
    commits_marked_as_synced.insert(commit_id);
    unsynced_commits.erase(
        std::remove_if(
            unsynced_commits.begin(), unsynced_commits.end(),
            [&commit_id](const std::unique_ptr<const storage::Commit>& commit) {
              return commit->GetId() == commit_id;
            }),
        unsynced_commits.end());
    callback(storage::Status::OK);
  }

  std::unique_ptr<TestCommit> NewCommit(std::string id, std::string content) {
    auto commit =
        std::make_unique<TestCommit>(std::move(id), std::move(content));
    unsynced_commits.push_back(commit->Clone());
    return commit;
  }

  std::map<storage::ObjectId, std::unique_ptr<const TestObject>>
      unsynced_objects_to_return;
  std::set<storage::ObjectId> objects_marked_as_synced;
  std::set<storage::CommitId> commits_marked_as_synced;
  std::vector<std::unique_ptr<const storage::Commit>> unsynced_commits;
};

// Fake implementation of cloud_provider_firebase::PageCloudHandler. Injects the
// returned status for the upload operations, allowing the test to make them
// fail. Registers the data uploaded by BatchUpload.
class TestPageCloudHandler
    : public cloud_provider_firebase::test::PageCloudHandlerEmptyImpl {
 public:
  explicit TestPageCloudHandler(fsl::MessageLoop* message_loop)
      : message_loop_(message_loop) {}

  ~TestPageCloudHandler() override = default;

  void AddCommits(const std::string& auth_token,
                  std::vector<cloud_provider_firebase::Commit> commits,
                  const std::function<void(cloud_provider_firebase::Status)>&
                      callback) override {
    add_commits_calls++;
    received_commit_tokens.push_back(auth_token);
    if (commit_status_to_return == cloud_provider_firebase::Status::OK) {
      std::move(commits.begin(), commits.end(),
                std::back_inserter(received_commits));
    }
    message_loop_->task_runner()->PostTask(
        [this, callback]() { callback(commit_status_to_return); });
  }

  void AddObject(
      const std::string& auth_token,
      cloud_provider_firebase::ObjectIdView object_id,
      zx::vmo data,
      std::function<void(cloud_provider_firebase::Status)> callback) override {
    add_object_calls++;
    received_object_tokens.push_back(auth_token);
    std::string received_data;
    ASSERT_TRUE(fsl::StringFromVmo(data, &received_data));
    received_objects.insert(
        std::make_pair(object_id.ToString(), received_data));
    fxl::Closure report_result =
        [ callback, status = object_status_to_return ] {
      callback(status);
    };
    if (delay_add_object_callbacks) {
      pending_add_object_callbacks.push_back(std::move(report_result));
    } else {
      message_loop_->task_runner()->PostTask(std::move(report_result));
    }

    if (reset_object_status_after_call) {
      object_status_to_return = cloud_provider_firebase::Status::OK;
    }
  }

  void RunPendingCallbacks() {
    for (auto& callback : pending_add_object_callbacks) {
      callback();
    }
    pending_add_object_callbacks.clear();
  }

  bool delay_add_object_callbacks = false;
  std::vector<fxl::Closure> pending_add_object_callbacks;
  cloud_provider_firebase::Status object_status_to_return =
      cloud_provider_firebase::Status::OK;
  bool reset_object_status_after_call = false;
  cloud_provider_firebase::Status commit_status_to_return =
      cloud_provider_firebase::Status::OK;
  unsigned int add_object_calls = 0u;
  unsigned int add_commits_calls = 0u;
  std::vector<std::string> received_commit_tokens;
  std::vector<cloud_provider_firebase::Commit> received_commits;
  std::vector<std::string> received_object_tokens;
  std::map<cloud_provider_firebase::ObjectId, std::string> received_objects;

 private:
  fsl::MessageLoop* message_loop_;
};

class BatchUploadTest : public ::test::TestWithMessageLoop {
 public:
  BatchUploadTest()
      : cloud_provider_(&message_loop_),
        auth_provider_(message_loop_.task_runner()) {}
  ~BatchUploadTest() override {}

 protected:
  TestPageStorage storage_;
  TestPageCloudHandler cloud_provider_;
  auth_provider::test::TestAuthProvider auth_provider_;

  unsigned int done_calls_ = 0u;
  unsigned int error_calls_ = 0u;

  std::unique_ptr<BatchUpload> MakeBatchUpload(
      std::vector<std::unique_ptr<const storage::Commit>> commits,
      unsigned int max_concurrent_uploads = 10) {
    return std::make_unique<BatchUpload>(&storage_, &cloud_provider_,
                                         &auth_provider_, std::move(commits),
                                         [this] {
                                           done_calls_++;
                                           message_loop_.PostQuitTask();
                                         },
                                         [this] {
                                           error_calls_++;
                                           message_loop_.PostQuitTask();
                                         },
                                         max_concurrent_uploads);
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BatchUploadTest);
};

// Test an upload of a single commit with no unsynced objects.
TEST_F(BatchUploadTest, SingleCommit) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(1u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id", cloud_provider_.received_commits.front().id);
  EXPECT_EQ("content", cloud_provider_.received_commits.front().content);
  EXPECT_TRUE(cloud_provider_.received_objects.empty());

  // Verify the sync status in storage.
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id"));
  EXPECT_EQ(0u, storage_.objects_marked_as_synced.size());
}

// Test an upload of multiple commits with no unsynced objects.
TEST_F(BatchUploadTest, MultipleCommits) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id0", "content0"));
  commits.push_back(storage_.NewCommit("id1", "content1"));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify that the commits were uploaded correctly and in a single call.
  EXPECT_EQ(1u, cloud_provider_.add_commits_calls);
  ASSERT_EQ(2u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id0", cloud_provider_.received_commits[0].id);
  EXPECT_EQ("content0", cloud_provider_.received_commits[0].content);
  EXPECT_EQ("id1", cloud_provider_.received_commits[1].id);
  EXPECT_EQ("content1", cloud_provider_.received_commits[1].content);
  EXPECT_TRUE(cloud_provider_.received_objects.empty());

  // Verify the sync status in storage.
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id0"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
}

// Test an upload of a commit with a few unsynced objects.
TEST_F(BatchUploadTest, SingleCommitWithObjects) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(1u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id", cloud_provider_.received_commits.front().id);
  EXPECT_EQ("content", cloud_provider_.received_commits.front().content);
  EXPECT_EQ(2u, cloud_provider_.received_objects.size());
  EXPECT_EQ("obj_data1", cloud_provider_.received_objects["obj_id1"]);
  EXPECT_EQ("obj_data2", cloud_provider_.received_objects["obj_id2"]);

  // Verify the sync status in storage.
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id"));
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id1"));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id2"));
}

// Verifies that auth tokens from auth provider are correctly passed to
// cloud provider.
TEST_F(BatchUploadTest, AuthTokens) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auth_provider_.token_to_return = "some-token";

  auto batch_upload = MakeBatchUpload(std::move(commits));
  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ((std::vector<std::string>{"some-token", "some-token"}),
            cloud_provider_.received_object_tokens);
  EXPECT_EQ(std::vector<std::string>{"some-token"},
            cloud_provider_.received_commit_tokens);
}

// Verifies that the error callback is called when the auth provider fails to
// return a token.
TEST_F(BatchUploadTest, AuthError) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auth_provider_.status_to_return = auth_provider::AuthStatus::ERROR;
  auth_provider_.token_to_return = "";

  auto batch_upload = MakeBatchUpload(std::move(commits));
  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, error_calls_);
  EXPECT_EQ(0u, done_calls_);
}

// Verifies that the number of concurrent object uploads is limited to
// |max_concurrent_uploads|.
TEST_F(BatchUploadTest, ThrottleConcurrentUploads) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id0"] =
      std::make_unique<TestObject>("obj_id0", "obj_data0");
  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  // Create the commit upload with |max_concurrent_uploads| = 2.
  auto batch_upload = MakeBatchUpload(std::move(commits), 2);

  cloud_provider_.delay_add_object_callbacks = true;
  batch_upload->Start();
  // TODO(ppi): how to avoid the wait?
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(50)));
  // Verify that only two object uploads are in progress.
  EXPECT_EQ(2u, cloud_provider_.add_object_calls);

  cloud_provider_.delay_add_object_callbacks = false;
  cloud_provider_.RunPendingCallbacks();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);
  EXPECT_EQ(3u, cloud_provider_.add_object_calls);
  EXPECT_EQ(3u, cloud_provider_.received_objects.size());
  EXPECT_EQ("obj_data0", cloud_provider_.received_objects["obj_id0"]);
  EXPECT_EQ("obj_data1", cloud_provider_.received_objects["obj_id1"]);
  EXPECT_EQ("obj_data2", cloud_provider_.received_objects["obj_id2"]);

  // Verify the sync status in storage.
  EXPECT_EQ(3u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id0"));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id1"));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id2"));
}

// Test un upload that fails on uploading objects.
TEST_F(BatchUploadTest, FailedObjectUpload) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  cloud_provider_.object_status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that no commits were uploaded.
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());

  // Verify that neither the objects nor the commit were marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_TRUE(storage_.objects_marked_as_synced.empty());
}

// Test an upload that fails on uploading commit commit.
TEST_F(BatchUploadTest, FailedCommitUpload) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  cloud_provider_.commit_status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that the objects were uploaded to cloud provider and marked as
  // synced.
  EXPECT_EQ(2u, cloud_provider_.received_objects.size());
  EXPECT_EQ("obj_data1", cloud_provider_.received_objects["obj_id1"]);
  EXPECT_EQ("obj_data2", cloud_provider_.received_objects["obj_id2"]);
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id1"));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id2"));

  // Verify that neither the commit wasn't marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
}

// Test an upload that fails and a subsequent retry that succeeds.
TEST_F(BatchUploadTest, ErrorAndRetry) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  cloud_provider_.object_status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, storage_.objects_marked_as_synced.size());

  // TestStorage moved the objects to be returned out, need to add them again
  // before retry.
  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");
  cloud_provider_.object_status_to_return = cloud_provider_firebase::Status::OK;
  batch_upload->Retry();
  ASSERT_FALSE(RunLoopWithTimeout());

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(1u, cloud_provider_.received_commits.size());
  EXPECT_EQ("id", cloud_provider_.received_commits.front().id);
  EXPECT_EQ("content", cloud_provider_.received_commits.front().content);
  EXPECT_EQ(2u, cloud_provider_.received_objects.size());
  EXPECT_EQ("obj_data1", cloud_provider_.received_objects["obj_id1"]);
  EXPECT_EQ("obj_data2", cloud_provider_.received_objects["obj_id2"]);

  // Verify the sync status in storage.
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id"));
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id1"));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count("obj_id2"));
}

// Verifies that if only one of many uploads fails, we still stop and notify the
// client.
TEST_F(BatchUploadTest, ErrorOneOfMultipleObject) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage_.unsynced_objects_to_return["obj_id0"] =
      std::make_unique<TestObject>("obj_id0", "obj_data0");
  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  cloud_provider_.object_status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;
  cloud_provider_.reset_object_status_after_call = true;
  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that two storage objects were correctly marked as synced.
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());

  // TestStorage moved the objects to be returned out, need to add them again
  // before retry.
  storage_.unsynced_objects_to_return["obj_id0"] =
      std::make_unique<TestObject>("obj_id0", "obj_data0");
  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  // Try upload again.
  batch_upload->Retry();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify the sync status in storage.
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(3u, storage_.objects_marked_as_synced.size());
}

// Verifies that we do not upload synced commits.
TEST_F(BatchUploadTest, DoNotUploadSyncedCommits) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(std::make_unique<TestCommit>("id", "content"));

  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify that the commit was not synced.
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
}

// Verifies that we do not upload synced commits on retries.
TEST_F(BatchUploadTest, DoNotUploadSyncedCommitsOnRetry) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  auto batch_upload = MakeBatchUpload(std::move(commits));

  cloud_provider_.commit_status_to_return =
      cloud_provider_firebase::Status::NETWORK_ERROR;

  batch_upload->Start();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that the commit was not synced.
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());

  // Mark commit as synced.
  storage::Status status;
  storage_.MarkCommitSynced("id", callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(0u, storage_.unsynced_commits.size());

  // Retry
  cloud_provider_.commit_status_to_return = cloud_provider_firebase::Status::OK;
  batch_upload->Retry();
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that the commit was not synced.
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());
}

}  // namespace

}  // namespace cloud_sync
