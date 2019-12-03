// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/batch_upload.h"

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <functional>
#include <map>
#include <utility>

#include <gmock/gmock.h>

#include "src/ledger/bin/cloud_sync/impl/entry_payload_encoding.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_storage.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/ledger/bin/storage/fake/fake_object_identifier_factory.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/capture.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_view.h"

namespace cloud_sync {
namespace {

using ::storage::fake::FakePiece;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::SizeIs;
using ::testing::Truly;

template <typename E>
class BaseBatchUploadTest : public ledger::TestWithEnvironment {
 public:
  BaseBatchUploadTest()
      : storage_(dispatcher()),
        encryption_service_(dispatcher()),
        page_cloud_(page_cloud_ptr_.NewRequest()) {}
  BaseBatchUploadTest(const BaseBatchUploadTest&) = delete;
  BaseBatchUploadTest& operator=(const BaseBatchUploadTest&) = delete;
  ~BaseBatchUploadTest() override = default;

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
    return MakeBatchUploadWithStorage(&storage_, std::move(commits), max_concurrent_uploads);
  }

  std::unique_ptr<BatchUpload> MakeBatchUploadWithStorage(
      storage::PageStorage* storage, std::vector<std::unique_ptr<const storage::Commit>> commits,
      unsigned int max_concurrent_uploads = 10) {
    return std::make_unique<BatchUpload>(
        environment_.coroutine_service(), storage, &encryption_service_, &page_cloud_ptr_,
        std::move(commits),
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
        storage_.GetObjectIdentifierFactory(), storage::ObjectDigest(std::move(object_digest)));
  }
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
  EXPECT_EQ(page_cloud_.received_commits.front().id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id")));
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
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id0")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "content0");
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
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
  EXPECT_EQ(page_cloud_.received_commits.front().id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id")));
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

class PageStorageBlockingGetUnsyncedPieces : public TestPageStorage {
 public:
  explicit PageStorageBlockingGetUnsyncedPieces(async_dispatcher_t* dispatcher)
      : TestPageStorage(dispatcher) {}

  void GetUnsyncedPieces(fit::function<void(ledger::Status, std::vector<storage::ObjectIdentifier>)>
                             callback) override {
    TestPageStorage::GetUnsyncedPieces(
        [this, callback = std::move(callback)](
            ledger::Status status, std::vector<storage::ObjectIdentifier> unsynced_pieces) mutable {
          get_unsynced_pieces_blocked_calls.push_back(
              [status, callback = std::move(callback),
               unsynced_pieces = std::move(unsynced_pieces)]() mutable {
                callback(status, std::move(unsynced_pieces));
              });
        });
  }

  std::vector<fit::closure> get_unsynced_pieces_blocked_calls;
};

// Test an upload of a commit with two unsynced objects, one of which gets GCed during the call to
// |GetUnsyncedPieces|.
TEST_F(BatchUploadTest, SingleCommitWithObjectsGC) {
  PageStorageBlockingGetUnsyncedPieces storage(dispatcher());

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage.NewCommit("id", "content", true));
  auto id1 = MakeObjectIdentifier("obj_digest1");
  auto id2 = MakeObjectIdentifier("obj_digest2");

  storage.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUploadWithStorage(&storage, std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 0u);

  // At this stage, GetUnsyncedPieces has collected id1 and id2 but the callback returning them is
  // blocked.
  ASSERT_THAT(storage.get_unsynced_pieces_blocked_calls, SizeIs(1));
  // Remove id2 to simulate a garbage-collection, ensuring the upcoming call to GetPiece will return
  // NOT_FOUND.
  storage.unsynced_objects_to_return.erase(id2);
  storage.get_unsynced_pieces_blocked_calls[0]();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify the artifacts uploaded to cloud provider. Only the first object has been uploaded.
  EXPECT_THAT(page_cloud_.received_commits, SizeIs(1));
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits.front().id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id")));
  EXPECT_EQ(
      encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits.front().data()),
      "content");
  EXPECT_THAT(page_cloud_.received_objects, SizeIs(1));
  EXPECT_EQ(encryption_service_.DecryptObjectSynchronous(
                page_cloud_.received_objects[encryption_service_.GetObjectNameSynchronous(id1)]),
            "obj_data1");

  // Verify the sync status in storage.
  EXPECT_THAT(storage.commits_marked_as_synced, ElementsAre("id"));
  EXPECT_THAT(storage.objects_marked_as_synced, ElementsAre(id1));
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

// Test an upload that fails on uploading objects with a permanent error.
TEST_F(BatchUploadTest, FailedObjectUploadPermanentError) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.object_status_to_return = cloud_provider::Status::NOT_FOUND;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::PERMANENT);

  // Verify that no commits were uploaded.
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);

  // Verify that neither the objects nor the commit were marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
  EXPECT_TRUE(storage_.objects_marked_as_synced.empty());
}

// Test an upload of a commit with a diff from the empty page.
TEST_F(BatchUploadTest, DiffFromEmpty) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  auto obj_id0 = MakeObjectIdentifier("obj_digest0");
  auto obj_id1 = MakeObjectIdentifier("obj_digest1");
  storage_.diffs_to_return["id"] =
      std::make_pair(storage::kFirstPageCommitId.ToString(),
                     std::vector<storage::EntryChange>{
                         {{"key0", obj_id0, storage::KeyPriority::EAGER, "entry0"}, false},
                         {{"key1", obj_id1, storage::KeyPriority::LAZY, "entry1"}, true}});

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(page_cloud_.received_commits.size(), 1u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits.front().id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id")));
  EXPECT_EQ(
      encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits.front().data()),
      "content");
  ASSERT_TRUE(page_cloud_.received_commits[0].has_diff());
  ASSERT_TRUE(page_cloud_.received_commits[0].diff().has_base_state());
  EXPECT_TRUE(page_cloud_.received_commits[0].diff().base_state().is_empty_page());
  EXPECT_TRUE(page_cloud_.received_commits[0].diff().has_changes());
  EXPECT_THAT(page_cloud_.received_commits[0].diff().changes(), SizeIs(2));

  auto& changes = page_cloud_.received_commits[0].diff().changes();
  ASSERT_TRUE(changes[0].has_operation());
  ASSERT_TRUE(changes[0].has_entry_id());
  ASSERT_TRUE(changes[0].has_data());
  EXPECT_EQ(changes[0].operation(), cloud_provider::Operation::INSERTION);
  storage::Entry entry0;
  std::string decrypted_entry0_payload =
      encryption_service_.DecryptEntryPayloadSynchronous(changes[0].data());
  ASSERT_TRUE(DecodeEntryPayload(changes[0].entry_id(), decrypted_entry0_payload,
                                 storage_.GetObjectIdentifierFactory(), &entry0));
  EXPECT_EQ(entry0, (storage::Entry{"key0", obj_id0, storage::KeyPriority::EAGER, "entry0"}));

  ASSERT_TRUE(changes[1].has_operation());
  ASSERT_TRUE(changes[1].has_entry_id());
  ASSERT_TRUE(changes[1].has_data());
  EXPECT_EQ(changes[1].operation(), cloud_provider::Operation::DELETION);
  EXPECT_EQ(changes[1].entry_id(), convert::ToArray("entry1"));
  storage::Entry entry1;

  std::string decrypted_entry1_payload =
      encryption_service_.DecryptEntryPayloadSynchronous(changes[1].data());

  ASSERT_TRUE(DecodeEntryPayload(changes[1].entry_id(), decrypted_entry1_payload,
                                 storage_.GetObjectIdentifierFactory(), &entry1));
  EXPECT_EQ(entry1, (storage::Entry{"key1", obj_id1, storage::KeyPriority::LAZY, "entry1"}));

  EXPECT_TRUE(page_cloud_.received_objects.empty());

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id"), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 0u);
}

// Test an upload of a commit with a diff from another commit uploaded in the same batch. Check that
// the commit ids are consistent.
TEST_F(BatchUploadTest, DiffFromCommit) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id1", "base_content", true));
  commits.push_back(storage_.NewCommit("id2", "content", true));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  storage_.diffs_to_return["id1"] = std::make_pair("id0", std::vector<storage::EntryChange>{});
  storage_.diffs_to_return["id2"] = std::make_pair("id1", std::vector<storage::EntryChange>{});

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify the artifacts uploaded to cloud provider.
  ASSERT_EQ(page_cloud_.received_commits.size(), 2u);
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits[0].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
  EXPECT_EQ(page_cloud_.received_commits[1].id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id2")));
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[0].data()),
            "base_content");
  EXPECT_EQ(encryption_service_.DecryptCommitSynchronous(page_cloud_.received_commits[1].data()),
            "content");

  // Check the diffs.
  ASSERT_TRUE(page_cloud_.received_commits[0].has_diff());
  ASSERT_TRUE(page_cloud_.received_commits[0].diff().has_base_state());
  ASSERT_TRUE(page_cloud_.received_commits[0].diff().base_state().is_at_commit());
  EXPECT_EQ(page_cloud_.received_commits[0].diff().base_state().at_commit(),
            convert::ToArray(encryption_service_.EncodeCommitId("id0")));
  ASSERT_TRUE(page_cloud_.received_commits[0].diff().has_changes());
  EXPECT_THAT(page_cloud_.received_commits[0].diff().changes(), SizeIs(0));

  ASSERT_TRUE(page_cloud_.received_commits[1].has_diff());
  ASSERT_TRUE(page_cloud_.received_commits[1].diff().has_base_state());
  ASSERT_TRUE(page_cloud_.received_commits[1].diff().base_state().is_at_commit());
  EXPECT_EQ(page_cloud_.received_commits[1].diff().base_state().at_commit(),
            convert::ToArray(encryption_service_.EncodeCommitId("id1")));
  ASSERT_TRUE(page_cloud_.received_commits[1].diff().has_changes());
  EXPECT_THAT(page_cloud_.received_commits[1].diff().changes(), SizeIs(0));

  // Check that the base commit id in the second diff is the remote commit id of the first commit.
  EXPECT_EQ(page_cloud_.received_commits[1].diff().base_state().at_commit(),
            page_cloud_.received_commits[0].id());

  EXPECT_TRUE(page_cloud_.received_objects.empty());

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 2u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id1"), 1u);
  EXPECT_EQ(storage_.commits_marked_as_synced.count("id2"), 1u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 0u);
}

TEST_F(BatchUploadTest, DiffSortedByEntryId) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));
  auto batch_upload = MakeBatchUpload(std::move(commits));

  auto obj_id0 = MakeObjectIdentifier("obj_digest0");
  auto obj_id1 = MakeObjectIdentifier("obj_digest1");
  storage_.diffs_to_return["id"] =
      std::make_pair(storage::kFirstPageCommitId.ToString(),
                     std::vector<storage::EntryChange>{
                         {{"key0", obj_id0, storage::KeyPriority::EAGER, "entryC"}, false},
                         {{"key1", obj_id1, storage::KeyPriority::LAZY, "entryA"}, true},
                         {{"key2", obj_id1, storage::KeyPriority::LAZY, "entryB"}, true}});

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 1u);
  EXPECT_EQ(error_calls_, 0u);

  // Verify that the entries are sent in entry id order.
  ASSERT_THAT(page_cloud_.received_commits, SizeIs(1));
  ASSERT_THAT(page_cloud_.received_commits, Each(Truly(CommitHasIdAndData)));
  EXPECT_EQ(page_cloud_.received_commits.front().id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id")));
  ASSERT_TRUE(page_cloud_.received_commits[0].has_diff());
  ASSERT_TRUE(page_cloud_.received_commits[0].diff().has_base_state());
  EXPECT_TRUE(page_cloud_.received_commits[0].diff().base_state().is_empty_page());
  EXPECT_TRUE(page_cloud_.received_commits[0].diff().has_changes());

  auto& changes = page_cloud_.received_commits[0].diff().changes();
  ASSERT_THAT(changes, SizeIs(3));
  ASSERT_TRUE(changes[0].has_entry_id());
  EXPECT_EQ(changes[0].entry_id(), convert::ToArray("entryA"));
  ASSERT_TRUE(changes[1].has_entry_id());
  EXPECT_EQ(changes[1].entry_id(), convert::ToArray("entryB"));
  ASSERT_TRUE(changes[2].has_entry_id());
  EXPECT_EQ(changes[2].entry_id(), convert::ToArray("entryC"));
}

TEST_F(BatchUploadTest, GetDiffFailure) {
  storage_.should_fail_get_diff_for_cloud = true;

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  storage_.diffs_to_return["id"] = std::make_pair("parent", std::vector<storage::EntryChange>{});

  auto batch_upload = MakeBatchUpload(std::move(commits));

  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::PERMANENT);

  // Verify the artifacts uploaded to cloud provider.
  EXPECT_EQ(page_cloud_.received_commits.size(), 0u);
  EXPECT_THAT(page_cloud_.received_objects, SizeIs(2));

  // Verify the sync status in storage.
  EXPECT_EQ(storage_.commits_marked_as_synced.size(), 0u);
  EXPECT_EQ(storage_.objects_marked_as_synced.size(), 2u);
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

  // Verify that the objects were uploaded to cloud provider and marked as synced.
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

  // Verify that the commit wasn't marked as synced.
  EXPECT_TRUE(storage_.commits_marked_as_synced.empty());
}

// Test an upload that fails permanently on uploading the commit.
TEST_F(BatchUploadTest, FailedCommitUploadPermanentError) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage_.NewCommit("id", "content", true));

  storage::ObjectIdentifier id1 = MakeObjectIdentifier("obj_digest1");
  storage::ObjectIdentifier id2 = MakeObjectIdentifier("obj_digest2");

  storage_.unsynced_objects_to_return[id1] = std::make_unique<FakePiece>(id1, "obj_data1");
  storage_.unsynced_objects_to_return[id2] = std::make_unique<FakePiece>(id2, "obj_data2");

  auto batch_upload = MakeBatchUpload(std::move(commits));

  page_cloud_.commit_status_to_return = cloud_provider::Status::NOT_FOUND;
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);
  EXPECT_EQ(last_error_type_, BatchUpload::ErrorType::PERMANENT);

  // Verify that the objects were uploaded to cloud provider and marked as synced.
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

  // Verify that neither commit was marked as synced.
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
  EXPECT_EQ(page_cloud_.received_commits.front().id(),
            convert::ToArray(encryption_service_.EncodeCommitId("id")));
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
TEST_F(BatchUploadTest, FailedObjectUploadWithStorageError) {
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

class PageStorageBlockingGetDiff : public TestPageStorage {
 public:
  explicit PageStorageBlockingGetDiff(async_dispatcher_t* dispatcher)
      : TestPageStorage(dispatcher) {}

  void GetDiffForCloud(
      const storage::Commit& commit,
      fit::function<void(ledger::Status, storage::CommitIdView, std::vector<storage::EntryChange>)>
          callback) override {
    get_diff_callbacks[commit.GetId()] = std::move(callback);
  }

  std::map<storage::CommitId, fit::function<void(ledger::Status, storage::CommitIdView,
                                                 std::vector<storage::EntryChange>)>>
      get_diff_callbacks;
};

TEST_F(BatchUploadTest, BatchUploadCancellation) {
  PageStorageBlockingGetDiff storage(dispatcher());

  // Test that, when cancelled during GetDiffFromCloud because of an error, we do not crash.
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(storage.NewCommit("bad_commit", "content", true));
  commits.push_back(storage.NewCommit("good_commit", "content", true));

  auto batch_upload = MakeBatchUploadWithStorage(&storage, std::move(commits));
  batch_upload->Start();
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 0u);
  EXPECT_THAT(storage.get_diff_callbacks, SizeIs(2));
  ASSERT_TRUE(storage.get_diff_callbacks["bad_commit"]);
  ASSERT_TRUE(storage.get_diff_callbacks["good_commit"]);

  storage.get_diff_callbacks["bad_commit"](ledger::Status::INTERNAL_NOT_FOUND, "", {});
  RunLoopUntilIdle();
  EXPECT_EQ(done_calls_, 0u);
  EXPECT_EQ(error_calls_, 1u);

  batch_upload.reset();
  auto id = MakeObjectIdentifier("obj_digest");
  storage.get_diff_callbacks["good_commit"](
      ledger::Status::OK, "other_commit",
      std::vector<storage::EntryChange>{{{"key", id, storage::KeyPriority::EAGER, "entry"}, true}});
  RunLoopUntilIdle();
  // This should not crash.
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

class FailingEncryptEntryPayloadEncryptionService : public encryption::FakeEncryptionService {
 public:
  explicit FailingEncryptEntryPayloadEncryptionService(async_dispatcher_t* dispatcher)
      : encryption::FakeEncryptionService(dispatcher) {}

  void EncryptEntryPayload(std::string /*entry_storage*/,
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
    ::testing::Types<FailingEncryptCommitEncryptionService,
                     FailingEncryptEntryPayloadEncryptionService, FailingGetNameEncryptionService,
                     FailingEncryptObjectEncryptionService>;

TYPED_TEST_SUITE(FailingBatchUploadTest, FailingEncryptionServices);

TYPED_TEST(FailingBatchUploadTest, Fail) {
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  commits.push_back(this->storage_.NewCommit("id", "content", true));
  auto id1 = this->MakeObjectIdentifier("obj_digest1");
  auto id2 = this->MakeObjectIdentifier("obj_digest2");

  this->storage_.diffs_to_return["id"] =
      std::make_pair(storage::kFirstPageCommitId.ToString(),
                     std::vector<storage::EntryChange>{
                         {{"key0", id1, storage::KeyPriority::EAGER, "entry0"}, false},
                         {{"key1", id2, storage::KeyPriority::LAZY, "entry1"}, true}});

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
