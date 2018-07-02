// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/batch_upload.h"

#include <functional>
#include <map>
#include <utility>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "lib/callback/capture.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/ledger/cloud_sync/impl/testing/test_page_cloud.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/testing/commit_empty_impl.h"
#include "peridot/bin/ledger/storage/testing/page_storage_empty_impl.h"

namespace cloud_sync {
namespace {

// Fake implementation of storage::Commit.
class TestCommit : public storage::CommitEmptyImpl {
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
  TestObject(storage::ObjectIdentifier identifier, std::string data)
      : identifier(std::move(identifier)), data(std::move(data)) {}
  ~TestObject() override = default;

  storage::ObjectIdentifier GetIdentifier() const override {
    return identifier;
  };

  storage::Status GetData(fxl::StringView* result) const override {
    *result = fxl::StringView(data);
    return storage::Status::OK;
  }

  storage::ObjectIdentifier identifier;
  std::string data;
};

// Fake implementation of storage::PageStorage. Injects the data that
// BatchUpload asks about: page id and unsynced objects to be uploaded.
// Registers the reported results of the upload: commits and objects marked as
// synced.
class TestPageStorage : public storage::PageStorageEmptyImpl {
 public:
  TestPageStorage() = default;
  ~TestPageStorage() override = default;

  void GetUnsyncedCommits(
      fit::function<void(storage::Status,
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
      fit::function<void(storage::Status,
                         std::vector<storage::ObjectIdentifier>)>
          callback) override {
    std::vector<storage::ObjectIdentifier> object_identifiers;
    for (auto& digest_object_pair : unsynced_objects_to_return) {
      object_identifiers.push_back(digest_object_pair.first);
    }
    callback(storage::Status::OK, std::move(object_identifiers));
  }

  void GetObject(storage::ObjectIdentifier object_identifier,
                 Location /*location*/,
                 fit::function<void(storage::Status,
                                    std::unique_ptr<const storage::Object>)>
                     callback) override {
    GetPiece(std::move(object_identifier), std::move(callback));
  }

  void GetPiece(storage::ObjectIdentifier object_identifier,
                fit::function<void(storage::Status,
                                   std::unique_ptr<const storage::Object>)>
                    callback) override {
    callback(
        storage::Status::OK,
        std::move(unsynced_objects_to_return[std::move(object_identifier)]));
  }

  void MarkPieceSynced(storage::ObjectIdentifier object_identifier,
                       fit::function<void(storage::Status)> callback) override {
    objects_marked_as_synced.insert(object_identifier);
    callback(storage::Status::OK);
  }

  void MarkCommitSynced(
      const storage::CommitId& commit_id,
      fit::function<void(storage::Status)> callback) override {
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

  std::map<storage::ObjectIdentifier, std::unique_ptr<const TestObject>>
      unsynced_objects_to_return;
  std::set<storage::ObjectIdentifier> objects_marked_as_synced;
  std::set<storage::CommitId> commits_marked_as_synced;
  std::vector<std::unique_ptr<const storage::Commit>> unsynced_commits;
};

// Fake implementation of storage::PageStorage. Fails when trying to mark
// objects as synced and can be used to verify behavior on object upload storage
// errors.
class TestPageStorageFailingToMarkPieces : public TestPageStorage {
 public:
  void MarkPieceSynced(storage::ObjectIdentifier /*object_identifier*/,
                       fit::function<void(storage::Status)> callback) override {
    callback(storage::Status::NOT_IMPLEMENTED);
  }
};

template <typename E>
class BaseBatchUploadTest : public gtest::TestLoopFixture {
 public:
  BaseBatchUploadTest()
      : encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()) {}
  ~BaseBatchUploadTest() override {}

 public:
  TestPageStorage storage_;
  E encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_ptr_;
  TestPageCloud page_cloud_;

  unsigned int done_calls_ = 0u;
  unsigned int error_calls_ = 0u;
  BatchUpload::ErrorType last_error_type_ = BatchUpload::ErrorType::PERMANENT;

  std::unique_ptr<BatchUpload> MakeBatchUpload(
      std::vector<std::unique_ptr<const storage::Commit>> commits,
      unsigned int max_concurrent_uploads = 10) {
    return MakeBatchUploadWithStorage(&storage_, std::move(commits),
                                      max_concurrent_uploads);
  }

  std::unique_ptr<BatchUpload> MakeBatchUploadWithStorage(
      storage::PageStorage* storage,
      std::vector<std::unique_ptr<const storage::Commit>> commits,
      unsigned int max_concurrent_uploads = 10) {
    return std::make_unique<BatchUpload>(
        storage, &encryption_service_, &page_cloud_ptr_, std::move(commits),
        [this] {
          done_calls_++;
          QuitLoop();
        },
        [this](BatchUpload::ErrorType error_type) {
          error_calls_++;
          last_error_type_ = error_type;
          QuitLoop();
        },
        max_concurrent_uploads);
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BaseBatchUploadTest);
};

using BatchUploadTest = BaseBatchUploadTest<encryption::FakeEncryptionService>;

// Test an upload of a single commit with no unsynced objects.
TEST_F(BatchUploadTest, SingleCommit) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(1u, page_cloud_.received_commits.size());
  EXPECT_EQ("id", page_cloud_.received_commits.front().id);
  EXPECT_EQ("content", encryption_service_.DecryptCommitSynchronous(
                           page_cloud_.received_commits.front().data));
  EXPECT_TRUE(page_cloud_.received_objects.empty());

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
  RunLoopUntilIdle();
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify that the commits were uploaded correctly and in a single call.
  EXPECT_EQ(1u, page_cloud_.add_commits_calls);
  ASSERT_EQ(2u, page_cloud_.received_commits.size());
  EXPECT_EQ("id0", page_cloud_.received_commits[0].id);
  EXPECT_EQ("content0", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[0].data));
  EXPECT_EQ("id1", page_cloud_.received_commits[1].id);
  EXPECT_EQ("content1", encryption_service_.DecryptCommitSynchronous(
                            page_cloud_.received_commits[1].data));
  EXPECT_TRUE(page_cloud_.received_objects.empty());

  // Verify the sync status in storage.
  EXPECT_EQ(2u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id0"));
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id1"));
}

// Test an upload of a commit with a few unsynced objects.
TEST_F(BatchUploadTest, SingleCommitWithObjects) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));
  auto id1 = encryption_service_.MakeObjectIdentifier("obj_digest1");
  auto id2 = encryption_service_.MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(1u, page_cloud_.received_commits.size());
  EXPECT_EQ("id", page_cloud_.received_commits.front().id);
  EXPECT_EQ("content", encryption_service_.DecryptCommitSynchronous(
                           page_cloud_.received_commits.front().data));
  EXPECT_EQ(2u, page_cloud_.received_objects.size());
  EXPECT_EQ(
      "obj_data1",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id1)]));
  EXPECT_EQ(
      "obj_data2",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id2)]));

  // Verify the sync status in storage.
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id"));
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id1));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id2));
}

// Verifies that the number of concurrent object uploads is limited to
// |max_concurrent_uploads|.
TEST_F(BatchUploadTest, ThrottleConcurrentUploads) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));
  storage::ObjectIdentifier id0 =
      encryption_service_.MakeObjectIdentifier("obj_digest0");
  storage::ObjectIdentifier id1 =
      encryption_service_.MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 =
      encryption_service_.MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id0] =
      std::make_unique<TestObject>(id0, "obj_data0");
  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  // Create the commit upload with |max_concurrent_uploads| = 2.
  auto batch_upload = MakeBatchUpload(std::move(commits), 2);

  page_cloud_.delay_add_object_callbacks = true;
  batch_upload->Start();
  RunLoopUntilIdle();
  // Verify that only two object uploads are in progress.
  EXPECT_EQ(2u, page_cloud_.add_object_calls);

  page_cloud_.delay_add_object_callbacks = false;
  page_cloud_.RunPendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);
  EXPECT_EQ(3u, page_cloud_.add_object_calls);
  EXPECT_EQ(3u, page_cloud_.received_objects.size());
  EXPECT_EQ(
      "obj_data0",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id0)]));
  EXPECT_EQ(
      "obj_data1",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id1)]));
  EXPECT_EQ(
      "obj_data2",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id2)]));

  // Verify the sync status in storage.
  EXPECT_EQ(3u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id0));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id1));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id2));
}

// Test an upload that fails on uploading objects.
TEST_F(BatchUploadTest, FailedObjectUpload) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage::ObjectIdentifier id1 =
      encryption_service_.MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 =
      encryption_service_.MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);
  EXPECT_EQ(BatchUpload::ErrorType::TEMPORARY, last_error_type_);

  // Verify that no commits were uploaded.
  EXPECT_EQ(0u, page_cloud_.received_commits.size());

  // Verify that neither the objects nor the commit were marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_TRUE(storage_.objects_marked_as_synced.empty());
}

// Test an upload that fails on uploading the commit.
TEST_F(BatchUploadTest, FailedCommitUpload) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage::ObjectIdentifier id1 =
      encryption_service_.MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 =
      encryption_service_.MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);
  EXPECT_EQ(BatchUpload::ErrorType::TEMPORARY, last_error_type_);

  // Verify that the objects were uploaded to cloud provider and marked as
  // synced.
  EXPECT_EQ(2u, page_cloud_.received_objects.size());
  EXPECT_EQ(
      "obj_data1",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id1)]));
  EXPECT_EQ(
      "obj_data2",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id2)]));
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id1));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id2));

  // Verify that neither the commit wasn't marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
}

// Test an upload that fails and a subsequent retry that succeeds.
TEST_F(BatchUploadTest, ErrorAndRetry) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage::ObjectIdentifier id1 =
      encryption_service_.MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 =
      encryption_service_.MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);
  EXPECT_EQ(BatchUpload::ErrorType::TEMPORARY, last_error_type_);

  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, storage_.objects_marked_as_synced.size());

  // TestStorage moved the objects to be returned out, need to add them again
  // before retry.
  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");
  page_cloud_.object_status_to_return = cloud_provider::Status::OK;
  batch_upload->Retry();
  RunLoopUntilIdle();

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(1u, page_cloud_.received_commits.size());
  EXPECT_EQ("id", page_cloud_.received_commits.front().id);
  EXPECT_EQ("content", encryption_service_.DecryptCommitSynchronous(
                           page_cloud_.received_commits.front().data));
  EXPECT_EQ(2u, page_cloud_.received_objects.size());
  EXPECT_EQ(
      "obj_data1",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id1)]));
  EXPECT_EQ(
      "obj_data2",
      encryption_service_.DecryptObjectSynchronous(
          page_cloud_.received_objects[encryption_service_
                                           .GetObjectNameSynchronous(id2)]));

  // Verify the sync status in storage.
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.commits_marked_as_synced.count("id"));
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id1));
  EXPECT_EQ(1u, storage_.objects_marked_as_synced.count(id2));
}

// Test a commit upload that gets an error from storage.
TEST_F(BatchUploadTest, FailedCommitUploadWitStorageError) {
  storage::PageStorageEmptyImpl test_storage;
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  auto batch_upload =
      MakeBatchUploadWithStorage(&test_storage, std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);
  EXPECT_EQ(BatchUpload::ErrorType::PERMANENT, last_error_type_);

  // Verify that no commits were uploaded.
  EXPECT_EQ(0u, page_cloud_.received_commits.size());
}

// Test objects upload that get an error from storage.
TEST_F(BatchUploadTest, FailedObjectUploadWitStorageError) {
  TestPageStorageFailingToMarkPieces test_storage;
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage::ObjectIdentifier id1 =
      encryption_service_.MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 =
      encryption_service_.MakeObjectIdentifier("obj_digest2");

  test_storage.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  test_storage.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  auto batch_upload =
      MakeBatchUploadWithStorage(&test_storage, std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);
  EXPECT_EQ(BatchUpload::ErrorType::PERMANENT, last_error_type_);

  // Verify that no commit or objects were uploaded.
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, storage_.objects_marked_as_synced.size());
}

// Verifies that if only one of many uploads fails, we still stop and notify the
// client.
TEST_F(BatchUploadTest, ErrorOneOfMultipleObject) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  storage::ObjectIdentifier id0 =
      encryption_service_.MakeObjectIdentifier("obj_digest0");
  storage::ObjectIdentifier id1 =
      encryption_service_.MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 =
      encryption_service_.MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id0] =
      std::make_unique<TestObject>(id0, "obj_data0");
  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  page_cloud_.reset_object_status_after_call = true;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that two storage objects were correctly marked as synced.
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(2u, storage_.objects_marked_as_synced.size());
  EXPECT_EQ(0u, page_cloud_.received_commits.size());

  // TestStorage moved the objects to be returned out, need to add them again
  // before retry.
  storage_.unsynced_objects_to_return[id0] =
      std::make_unique<TestObject>(id0, "obj_data0");
  storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  // Try upload again.
  batch_upload->Retry();
  RunLoopUntilIdle();
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
  RunLoopUntilIdle();
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(0u, error_calls_);

  // Verify that the commit was not synced.
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());
  EXPECT_EQ(0u, page_cloud_.received_commits.size());
}

// Verifies that we do not upload synced commits on retries.
TEST_F(BatchUploadTest, DoNotUploadSyncedCommitsOnRetry) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content"));

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(0u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that the commit was not synced.
  EXPECT_EQ(0u, storage_.commits_marked_as_synced.size());

  // Mark commit as synced.
  storage::Status status;
  storage_.MarkCommitSynced("id",
                            callback::Capture(QuitLoopClosure(), &status));
  RunLoopUntilIdle();
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(0u, storage_.unsynced_commits.size());

  // Retry.
  page_cloud_.add_commits_calls = 0;
  page_cloud_.commit_status_to_return = cloud_provider::Status::OK;
  batch_upload->Retry();
  RunLoopUntilIdle();
  EXPECT_EQ(1u, done_calls_);
  EXPECT_EQ(1u, error_calls_);

  // Verify that no calls were made to attempt to upload the commit.
  EXPECT_EQ(0u, page_cloud_.add_commits_calls);
}

class FailingEncryptCommitEncryptionService
    : public encryption::FakeEncryptionService {
 public:
  explicit FailingEncryptCommitEncryptionService(async_t* async)
      : encryption::FakeEncryptionService(async) {}

  void EncryptCommit(
      std::string /*commit_storage*/,
      fit::function<void(encryption::Status, std::string)> callback) override {
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
      fit::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

class FailingEncryptObjectEncryptionService
    : public encryption::FakeEncryptionService {
 public:
  explicit FailingEncryptObjectEncryptionService(async_t* async)
      : encryption::FakeEncryptionService(async) {}

  void EncryptObject(
      storage::ObjectIdentifier /*object_identifier*/,
      fsl::SizedVmo /*content*/,
      fit::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

template <typename E>
using FailingBatchUploadTest = BaseBatchUploadTest<E>;

using FailingEncryptionServices =
    ::testing::Types<FailingEncryptCommitEncryptionService,
                     FailingGetNameEncryptionService,
                     FailingEncryptObjectEncryptionService>;

TYPED_TEST_CASE(FailingBatchUploadTest, FailingEncryptionServices);

TYPED_TEST(FailingBatchUploadTest, Fail) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(this->storage_.NewCommit("id", "content"));
  auto id1 = this->encryption_service_.MakeObjectIdentifier("obj_digest1");
  auto id2 = this->encryption_service_.MakeObjectIdentifier("obj_digest2");

  this->storage_.unsynced_objects_to_return[id1] =
      std::make_unique<TestObject>(id1, "obj_data1");
  this->storage_.unsynced_objects_to_return[id2] =
      std::make_unique<TestObject>(id2, "obj_data2");

  auto batch_upload = this->MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  this->RunLoopUntilIdle();
  EXPECT_EQ(0u, this->done_calls_);
  EXPECT_GE(this->error_calls_, 1u);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(0u, this->page_cloud_.received_commits.size());
}

}  // namespace

}  // namespace cloud_sync
