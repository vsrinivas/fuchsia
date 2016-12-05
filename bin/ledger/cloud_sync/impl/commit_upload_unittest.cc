// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/commit_upload.h"

#include <functional>
#include <unordered_map>
#include <utility>

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_provider/test/cloud_provider_empty_impl.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/test/commit_empty_impl.h"
#include "apps/ledger/src/storage/test/page_storage_empty_impl.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace cloud_sync {
namespace {

// Fake implementation of storage::Commit.
class TestCommit : public storage::test::CommitEmptyImpl {
 public:
  TestCommit() = default;
  ~TestCommit() override = default;

  const storage::CommitId& GetId() const override { return id; }

  std::string GetStorageBytes() const override { return storage_bytes; }

  storage::CommitId id;
  std::string storage_bytes;
};

// Fake implementation of storage::Object.
class TestObject : public storage::Object {
 public:
  TestObject() = default;
  TestObject(storage::ObjectId id, std::string data) : id(id), data(data) {}
  ~TestObject() override = default;

  storage::ObjectId GetId() const override { return id; };

  storage::Status GetData(ftl::StringView* result) const override {
    *result = ftl::StringView(data);
    return storage::Status::OK;
  }

  storage::ObjectId id;
  std::string data;
};

// Fake implementation of storage::PageStorage. Injects the data that
// CommitUpload asks about: page id and unsynced objects to be uploaded.
// Registers the reported results of the upload: commits and objects marked as
// synced.
class TestPageStorage : public storage::test::PageStorageEmptyImpl {
 public:
  TestPageStorage() = default;
  ~TestPageStorage() override = default;

  storage::Status GetUnsyncedObjects(
      const storage::CommitId& commit_id,
      std::vector<storage::ObjectId>* object_ids) override {
    object_ids->clear();
    for (auto& id_object_pair : unsynced_objects_to_return) {
      object_ids->push_back(id_object_pair.first);
    }
    return storage::Status::OK;
  }

  void GetObject(
      storage::ObjectIdView object_id,
      const std::function<void(storage::Status,
                               std::unique_ptr<const storage::Object>)>&
          callback) override {
    callback(storage::Status::OK,
             std::move(unsynced_objects_to_return[object_id.ToString()]));
  }

  storage::Status MarkObjectSynced(storage::ObjectIdView object_id) override {
    objects_marked_as_synced.insert(object_id.ToString());
    return storage::Status::OK;
  }

  storage::Status MarkCommitSynced(
      const storage::CommitId& commit_id) override {
    commits_marked_as_synced.insert(commit_id);
    return storage::Status::OK;
  }

  std::unordered_map<storage::ObjectId, std::unique_ptr<const TestObject>>
      unsynced_objects_to_return;
  std::set<storage::ObjectId> objects_marked_as_synced;
  std::set<storage::CommitId> commits_marked_as_synced;
};

// Fake implementation of cloud_provider::CloudProvider. Injects the returned
// status for the upload operations, allowing the test to make them fail.
// Registers the data uploaded by CommitUpload.
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

  void AddObject(
      cloud_provider::ObjectIdView object_id,
      mx::vmo data,
      std::function<void(cloud_provider::Status)> callback) override {
    std::string received_data;
    ASSERT_TRUE(mtl::StringFromVmo(std::move(data), &received_data));
    received_objects.insert(
        std::make_pair(object_id.ToString(), received_data));
    message_loop_->task_runner()->PostTask(
        [this, callback]() { callback(object_status_to_return); });
  }

  cloud_provider::Status object_status_to_return = cloud_provider::Status::OK;
  cloud_provider::Status commit_status_to_return = cloud_provider::Status::OK;
  std::vector<cloud_provider::Commit> received_commits;
  std::map<cloud_provider::ObjectId, std::string> received_objects;

 private:
  mtl::MessageLoop* message_loop_;
};

class CommitUploadTest : public ::testing::Test {
 public:
  CommitUploadTest() : cloud_provider_(&message_loop_) {}
  ~CommitUploadTest() override {}

 protected:
  mtl::MessageLoop message_loop_;
  TestPageStorage storage_;
  TestCloudProvider cloud_provider_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CommitUploadTest);
};

// Test an upload of a commit with no unsynced objects.
TEST_F(CommitUploadTest, NoObjects) {
  auto commit = std::make_unique<TestCommit>();
  commit->id = "id";
  commit->storage_bytes = "content";

  auto done_calls = 0u;
  auto error_calls = 0u;
  CommitUpload commit_upload(&storage_, &cloud_provider_, std::move(commit),
                             [this, &done_calls] {
                               done_calls++;
                               message_loop_.PostQuitTask();
                             },
                             [this, &error_calls] {
                               error_calls++;
                               message_loop_.PostQuitTask();
                             });

  commit_upload.Start();
  message_loop_.Run();
  EXPECT_EQ(1u, done_calls);
  EXPECT_EQ(0u, error_calls);

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

// Test an upload of a commit with a few unsynced objects.
TEST_F(CommitUploadTest, WithObjects) {
  auto commit = std::make_unique<TestCommit>();
  commit->id = "id";
  commit->storage_bytes = "content";

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto done_calls = 0u;
  auto error_calls = 0u;
  CommitUpload commit_upload(&storage_, &cloud_provider_, std::move(commit),
                             [this, &done_calls] {
                               done_calls++;
                               message_loop_.PostQuitTask();
                             },
                             [this, &error_calls] {
                               error_calls++;
                               message_loop_.PostQuitTask();
                             });

  commit_upload.Start();
  message_loop_.Run();
  EXPECT_EQ(1u, done_calls);
  EXPECT_EQ(0u, error_calls);

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

// Test un upload that fails on uploading objects.
TEST_F(CommitUploadTest, FailedObjectUpload) {
  auto commit = std::make_unique<TestCommit>();
  commit->id = "id";
  commit->storage_bytes = "content";

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto done_calls = 0u;
  auto error_calls = 0u;
  CommitUpload commit_upload(&storage_, &cloud_provider_, std::move(commit),
                             [this, &done_calls] {
                               done_calls++;
                               message_loop_.PostQuitTask();
                             },
                             [this, &error_calls] {
                               error_calls++;
                               message_loop_.PostQuitTask();
                             });

  cloud_provider_.object_status_to_return =
      cloud_provider::Status::NETWORK_ERROR;
  commit_upload.Start();
  message_loop_.Run();
  EXPECT_EQ(0u, done_calls);
  EXPECT_EQ(1u, error_calls);

  // Verify that no commits were uploaded.
  EXPECT_EQ(0u, cloud_provider_.received_commits.size());

  // Verify that neither the objects nor the commit were marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_TRUE(storage_.objects_marked_as_synced.empty());
}

// Test an upload that fails on uploading commit commit.
TEST_F(CommitUploadTest, FailedCommitUpload) {
  auto commit = std::make_unique<TestCommit>();
  commit->id = "id";
  commit->storage_bytes = "content";

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto done_calls = 0u;
  auto error_calls = 0u;
  CommitUpload commit_upload(&storage_, &cloud_provider_, std::move(commit),
                             [this, &done_calls] {
                               done_calls++;
                               message_loop_.PostQuitTask();
                             },
                             [this, &error_calls] {
                               error_calls++;
                               message_loop_.PostQuitTask();
                             });

  cloud_provider_.commit_status_to_return =
      cloud_provider::Status::NETWORK_ERROR;
  commit_upload.Start();
  message_loop_.Run();
  EXPECT_EQ(0u, done_calls);
  EXPECT_EQ(1u, error_calls);

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
TEST_F(CommitUploadTest, ErrorAndRetry) {
  auto commit = std::make_unique<TestCommit>();
  commit->id = "id";
  commit->storage_bytes = "content";

  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");

  auto done_calls = 0u;
  auto error_calls = 0u;
  CommitUpload commit_upload(&storage_, &cloud_provider_, std::move(commit),
                             [this, &done_calls] {
                               done_calls++;
                               message_loop_.PostQuitTask();
                             },
                             [this, &error_calls] {
                               error_calls++;
                               message_loop_.PostQuitTask();
                             });

  cloud_provider_.object_status_to_return =
      cloud_provider::Status::NETWORK_ERROR;
  commit_upload.Start();
  message_loop_.Run();
  EXPECT_EQ(0u, done_calls);
  EXPECT_EQ(1u, error_calls);

  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, storage_.objects_marked_as_synced.size());

  // TestStorage moved the objects to be returned out, need to add them again
  // before retry.
  storage_.unsynced_objects_to_return["obj_id1"] =
      std::make_unique<TestObject>("obj_id1", "obj_data1");
  storage_.unsynced_objects_to_return["obj_id2"] =
      std::make_unique<TestObject>("obj_id2", "obj_data2");
  cloud_provider_.object_status_to_return = cloud_provider::Status::OK;
  commit_upload.Start();
  message_loop_.Run();

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

}  // namespace

}  // namespace cloud_sync
