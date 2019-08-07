// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/batch_upload.h"

#include <lib/async/dispatcher.h>
#include <lib/callback/capture.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/test_loop_fixture.h>

#include <functional>
#include <map>
#include <utility>

#include <gmock/gmock.h>

#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace cloud_sync {
namespace {

using ::storage::fake::FakeObject;
using ::storage::fake::FakePiece;
using ::testing::Each;
using ::testing::Truly;

template <typename E>
class BaseBatchUploadTest : public gtest::TestLoopFixture {
 public:
  BaseBatchUploadTest()
      : storage_(dispatcher()),
        encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()) {}
  ~BaseBatchUploadTest() override {}

 public:
  TestPageStorage storage_;
  storage::fake::FakeObjectIdentifierFactory object_identifier_factory_;
  E encryption_service_;
  cloud_provider::PageCloudPtr page_cloud_ptr_;
  TestPageCloud page_cloud_;

  unsigned int done_calls_ = 0u;
  unsigned int error_calls_ = 0u;
  BatchUpload::ErrorType last_error_type_ = BatchUpload::ErrorType::PERMANENT;

  std::unique_ptr<BatchUpload> MakeBatchUpload(
      std::vector<std::unique_ptr<const storage::Commit>> commits,
      unsigned int max_concurrent_uploads = 10) {
    return MakeBatchUploadWithStorage(&storage_, std::move(commits), max_concurrent_uploads);
  }

  std::unique_ptr<BatchUpload> MakeBatchUploadWithStorage(
      storage::PageStorage* storage, std::vector<std::unique_ptr<const storage::Commit>> commits,
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

  // Returns an object identifier for the provided fake |object_digest|.
  // |object_digest| does not need to be valid (wrt. internal storage constraints) as it
  // is only used as an opaque identifier for cloud_sync.
  storage::ObjectIdentifier MakeObjectIdentifier(std::string object_digest) {
    return encryption_service_.MakeObjectIdentifier(
        &object_identifier_factory_, storage::ObjectDigest(std::move(object_digest)));
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BaseBatchUploadTest);
};

using BatchUploadTest = BaseBatchUploadTest<encryption::FakeEncryptionService>;

// Test an upload of a single commit with no unsynced objects.
TEST_F(BatchUploadTest, SingleCommit) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(page_cloud_.received_commits.size(), 1u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits.front().id(), convert::ToArray("id"));
  EXPECT_EQ(
      encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits.front().data()),
      "content");
  EXPECT_TRUE(page_cloud_.received_objects.empty());

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id"), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 0u);
}

// Test an upload of multiple commits with no unsynced objects.
TEST_F(BatchUploadTest, MultipleCommits) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id0", "content0", true));
  commits.push_back(storage_.NewCommit("id1", "content1", true));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify that the commits were uploaded correctly and in a single call.
  EXPECT_EQ(page_cloud_.add_commits_calls, 1u);
  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(), convert::ToArray("id0"));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content0");
  EXPECT_EQ(page_cloud_.received_commits[1].id(), convert::ToArray("id1"));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content1");
  EXPECT_TRUE(page_cloud_.received_objects.empty());

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id0"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 1u);
}

// Test an upload of a commit with a few unsynced objects.
TEST_F(BatchUploadTest, SingleCommitWithObjects) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));
  auto id1 = MakeObjectIdentifier("obj_digest1");
  auto id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(page_cloud_.received_commits.size(), 1u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits.front().id(), convert::ToArray("id"));
  EXPECT_EQ(
      encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits.front().data()),
      "content");
  EXPECT_EQ(page_cloud_.received_objects.size(), 2u);
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id1)]),
            "obj_data1");
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id2)]),
            "obj_data2");

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id"), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id1), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id2), 1u);
}

// Verifies that the number of concurrent object uploads is limited to
// |max_concurrent_uploads|.
TEST_F(BatchUploadTest, ThrottleConcurrentUploads) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));
  storage::ObjectIdentifier id0 = MakeObjectIdentifier("obj_digest0");
  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id0] = std::make_unique<FakePiece>(id0, "obj_data0");
  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  // Create the commit upload with |max_concurrent_uploads| = 2.
  auto batch_upload = MakeBatchUpload(std::move(commits), 2);

  page_cloud_.delay_add_object_callbacks = true;
  batch_upload->Start();
  RunLoopUntilIdle();
  // Verify that only two object uploads are in progress.
  EXPECT_EQ(page_cloud_.add_object_calls, 2u);

  page_cloud_.delay_add_object_callbacks = false;
  page_cloud_.RunPendingCallbacks();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);
  EXPECT_EQ(page_cloud_.add_object_calls, 3u);
  EXPECT_EQ(page_cloud_.received_objects.size(), 3u);
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id0)]),
            "obj_data0");
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id1)]),
            "obj_data1");
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id2)]),
            "obj_data2");

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 3u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id0), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id1), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id2), 1u);
}

// Test an upload that fails on uploading objects.
TEST_F(BatchUploadTest, FailedObjectUpload) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::TEMPORARY);

  // Verify that no commits were uploaded.
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);

  // Verify that neither the objects nor the commit were marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_TRUE(storage_.objects_marked_as_synced.empty());
}

// Test an upload that fails on uploading the commit.
TEST_F(BatchUploadTest, FailedCommitUpload) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::TEMPORARY);

  // Verify that the objects were uploaded to cloud provider and marked as
  // synced.
  EXPECT_EQ(page_cloud_.received_objects.size(), 2u);
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id1)]),
            "obj_data1");
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id2)]),
            "obj_data2");
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id1), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id2), 1u);

  // Verify that neither the commit wasn't marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
}

// Test an upload that fails and a subsequent retry that succeeds.
TEST_F(BatchUploadTest, ErrorAndRetry) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::TEMPORARY);

  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 0u);

  // TestStorage moved the objects to be returned out, need to add them again
  // before retry.
  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");
  page_cloud_.object_status_to_return = cloud_provider::Status::OK;
  batch_upload->Retry();
  RunLoopUntilIdle();

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(page_cloud_.received_commits.size(), 1u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits.front().id(), convert::ToArray("id"));
  EXPECT_EQ(
      encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits.front().data()),
      "content");
  EXPECT_EQ(page_cloud_.received_objects.size(), 2u);
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id1)]),
            "obj_data1");
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id2)]),
            "obj_data2");

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id"), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id1), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.count(id2), 1u);
}

// Test a commit upload that gets an error from storage.
TEST_F(BatchUploadTest, FailedCommitUploadWithStorageError) {
  storage_.should_fail_get_unsynced_pieces = true;

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  auto batch_upload = MakeBatchUploadWithStorage(&storage_, std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::PERMANENT);

  // Verify that no commits were uploaded.
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);
}

// Test objects upload that get an error from storage.
TEST_F(BatchUploadTest, FailedObjectUploadWitStorageError) {
  storage_.should_fail_mark_piece_synced = true;

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUploadWithStorage(&storage_, std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::PERMANENT);

  // Verify that no commit or objects were uploaded.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 0u);
}

// Verifies that if only one of many uploads fails, we still stop and notify the
// client.
TEST_F(BatchUploadTest, ErrorOneOfMultipleObject) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id0 = MakeObjectIdentifier("obj_digest0");
  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id0] = std::make_unique<FakePiece>(id0, "obj_data0");
  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.object_status_to_return = cloud_provider::Status::NETWORK_ERROR;
  page_cloud_.reset_object_status_after_call = true;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);

  // Verify that two storage objects were correctly marked as synced.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 2u);
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);

  // TestStorage moved the objects to be returned out, need to add them again
  // before retry.
  storage_.unsynced_objects_to_return[id0] = std::make_unique<FakePiece>(id0, "obj_data0");
  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  // Try upload again.
  batch_upload->Retry();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 1u);

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 3u);
}

// Verifies that we do not upload synced commits.
TEST_F(BatchUploadTest, DoNotUploadSyncedCommits) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(std::make_unique<TestCommit>("id", "content"));

  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify that the commit was not synced.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);
}

// Verifies that we do not upload synced commits on retries.
TEST_F(BatchUploadTest, DoNotUploadSyncedCommitsOnRetry) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.commit_status_to_return = cloud_provider::Status::NETWORK_ERROR;

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);

  // Verify that the commit was not synced.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);

  // Mark commit as synced.
  ledger::Status status;
  storage_.MarkCommitSynced("id", callback::Capture(QuitLoopClosure(), &status));
  RunLoopUntilIdle();
  EXPECT_EQ(status, ledger::Status::OK);
  EXPECT_EQ(storage_.unsynced_commits_to_return.size(), 0u);

  // Retry.
  page_cloud_.add_commits_calls = 0;
  page_cloud_.commit_status_to_return = cloud_provider::Status::OK;
  batch_upload->Retry();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 1u);

  // Verify that no calls were made to attempt to upload the commit.
  EXPECT_EQ(page_cloud_.add_commits_calls, 0u);
}

class FailingEncryptCommitEncryptionService : public encryption::FakeEncryptionService {
 public:
  explicit FailingEncryptCommitEncryptionService(async_dispatcher_t* dispatcher)
      : encryption::FakeEncryptionService(dispatcher) {}

  void EncryptCommit(std::string /*commit_storage*/,
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

class FailingEncryptObjectEncryptionService : public encryption::FakeEncryptionService {
 public:
  explicit FailingEncryptObjectEncryptionService(async_dispatcher_t* dispatcher)
      : encryption::FakeEncryptionService(dispatcher) {}

  void EncryptObject(storage::ObjectIdentifier /*object_identifier*/, fxl::StringView /*content*/,
                     fit::function<void(encryption::Status, std::string)> callback) override {
    callback(encryption::Status::INVALID_ARGUMENT, "");
  }
};

template <typename E>
using FailingBatchUploadTest = BaseBatchUploadTest<E>;

using FailingEncryptionServices =
    ::testing::Types<FailingEncryptCommitEncryptionService, FailingGetNameEncryptionService,
                     FailingEncryptObjectEncryptionService>;

TYPED_TEST_SUITE(FailingBatchUploadTest, FailingEncryptionServices);

TYPED_TEST(FailingBatchUploadTest, Fail) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(this->storage_.NewCommit("id", "content", true));
  auto id1 = this->MakeObjectIdentifier("obj_digest1");
  auto id2 = this->MakeObjectIdentifier("obj_digest2");

  this->storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  this->storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = this->MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  this->RunLoopUntilIdle();
  EXPECT_EQ(this->done_calls_, 0u);
  EXPECT_GE(this->error_calls_, 1u);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(this->page_cloud_.received_commits.size(), 0u);
}

}  // namespace

}  // namespace cloud_sync
