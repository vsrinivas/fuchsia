// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_db.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/impl/commit_factory.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/db_serialization.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/page_db_impl.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/fxl/macros.h"

namespace storage {
namespace {

using ::coroutine::CoroutineHandler;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

std::unique_ptr<LevelDb> GetLevelDb(async_dispatcher_t* dispatcher, ledger::DetachedPath db_path) {
  auto db = std::make_unique<LevelDb>(dispatcher, std::move(db_path));
  EXPECT_EQ(db->Init(), Status::OK);
  return db;
}

// Garbage collection is disabled for these tests because we are using object identifiers generated
// from the page storage in another database.
class PageDbTest : public ledger::TestWithEnvironment {
 public:
  PageDbTest()
      : ledger::TestWithEnvironment([](ledger::EnvironmentBuilder* builder) {
          builder->SetGcPolicy(GarbageCollectionPolicy::NEVER);
        }),
        encryption_service_(dispatcher()),
        base_path(tmpfs_.root_fd()),
        page_storage_(&environment_, &encryption_service_,
                      GetLevelDb(dispatcher(), base_path.SubPath("storage")), "page_id",
                      CommitPruningPolicy::NEVER),
        page_db_(&environment_, page_storage_.GetObjectIdentifierFactory(),
                 GetLevelDb(dispatcher(), base_path.SubPath("page_db"))) {}

  ~PageDbTest() override = default;

  // Test:
  void SetUp() override {
    Status status;
    bool called;
    clocks::DeviceIdManagerEmptyImpl device_id_manager;
    page_storage_.Init(&device_id_manager,
                       callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
  }

  ObjectIdentifier RandomObjectIdentifier() {
    return storage::RandomObjectIdentifier(environment_.random(),
                                           page_storage_.GetObjectIdentifierFactory());
  }

  // Utility function to delete commit |commit_id|. PageDb::DeleteCommit cannot be called directly,
  // the implementation requires it to be part of a batch.
  Status DeleteCommit(CoroutineHandler* handler, const CommitId& commit_id,
                      fxl::StringView remote_commit_id, const ObjectDigest& root_node_digest) {
    std::unique_ptr<PageDbImpl::Batch> batch;
    RETURN_ON_ERROR(page_db_.StartBatch(handler, &batch));
    RETURN_ON_ERROR(batch->DeleteCommit(
        handler, commit_id, remote_commit_id,
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, root_node_digest)));
    return batch->Execute(handler);
  }

 protected:
  scoped_tmpfs::ScopedTmpFS tmpfs_;
  encryption::FakeEncryptionService encryption_service_;
  ledger::DetachedPath base_path;
  PageStorageImpl page_storage_;
  PageDbImpl page_db_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDbTest);
};

TEST_F(PageDbTest, HeadCommits) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<std::pair<zx::time_utc, CommitId>> heads;
    EXPECT_EQ(page_db_.GetHeads(handler, &heads), Status::OK);
    EXPECT_TRUE(heads.empty());

    CommitId cid = RandomCommitId(environment_.random());
    EXPECT_EQ(page_db_.AddHead(handler, cid, environment_.random()->Draw<zx::time_utc>()),
              Status::OK);
    EXPECT_EQ(page_db_.GetHeads(handler, &heads), Status::OK);
    EXPECT_EQ(heads.size(), 1u);
    EXPECT_EQ(heads[0].second, cid);

    EXPECT_EQ(page_db_.RemoveHead(handler, cid), Status::OK);
    EXPECT_EQ(page_db_.GetHeads(handler, &heads), Status::OK);
    EXPECT_TRUE(heads.empty());
  });
}

TEST_F(PageDbTest, MergeCommits) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId parent1 = RandomCommitId(environment_.random());
    CommitId parent2 = RandomCommitId(environment_.random());
    CommitId merge1 = RandomCommitId(environment_.random());
    CommitId merge2 = RandomCommitId(environment_.random());
    std::vector<CommitId> merges;

    // There are no merges
    EXPECT_EQ(page_db_.GetMerges(handler, parent1, parent2, &merges), Status::OK);
    EXPECT_THAT(merges, IsEmpty());

    // Add two merges, check they are returned for both orders of the parents
    std::unique_ptr<PageDbImpl::Batch> batch;
    EXPECT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->AddMerge(handler, parent1, parent2, merge1), Status::OK);
    EXPECT_EQ(batch->AddMerge(handler, parent2, parent1, merge2), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    EXPECT_EQ(page_db_.GetMerges(handler, parent1, parent2, &merges), Status::OK);
    EXPECT_THAT(merges, UnorderedElementsAre(merge1, merge2));
    EXPECT_EQ(page_db_.GetMerges(handler, parent2, parent1, &merges), Status::OK);
    EXPECT_THAT(merges, UnorderedElementsAre(merge1, merge2));
  });
}

TEST_F(PageDbTest, OrderHeadCommitsByTimestampThenId) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Produce 10 random timestamps and 3 constants.
    std::vector<zx::time_utc> timestamps(10);
    std::generate(timestamps.begin(), timestamps.end(),
                  [this] { return environment_.random()->Draw<zx::time_utc>(); });
    timestamps.insert(timestamps.end(),
                      {zx::time_utc::infinite_past(), zx::time_utc::infinite(), zx::time_utc()});

    // Generate 10 commits per timestamp.
    std::vector<std::pair<zx::time_utc, CommitId>> commits;
    for (auto ts : timestamps) {
      for (size_t i = 0; i < 10; ++i) {
        CommitId id = RandomCommitId(environment_.random());
        commits.emplace_back(ts, id);
      }
    }

    // Insert the commits in random order.
    auto rng = environment_.random()->NewBitGenerator<uint64_t>();
    std::shuffle(commits.begin(), commits.end(), rng);
    for (auto [ts, id] : commits) {
      EXPECT_EQ(page_db_.AddHead(handler, id, ts), Status::OK);
    }

    // Check that GetHeads returns sorted commits.
    std::vector<std::pair<zx::time_utc, CommitId>> heads;
    EXPECT_EQ(page_db_.GetHeads(handler, &heads), Status::OK);
    std::sort(commits.begin(), commits.end());
    for (size_t i = 0; i < commits.size(); ++i) {
      EXPECT_EQ(heads[i].second, commits[i].second);
    }
  });
}

TEST_F(PageDbTest, Commits) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<std::unique_ptr<const Commit>> parents;
    parents.emplace_back(std::make_unique<CommitRandomImpl>(
        environment_.random(), page_storage_.GetObjectIdentifierFactory()));
    CommitFactory factory(page_storage_.GetObjectIdentifierFactory());

    std::unique_ptr<const Commit> commit = factory.FromContentAndParents(
        environment_.clock(), environment_.random(), RandomObjectIdentifier(), std::move(parents));

    std::string storage_bytes;
    EXPECT_EQ(page_db_.GetCommitStorageBytes(handler, commit->GetId(), &storage_bytes),
              Status::INTERNAL_NOT_FOUND);

    EXPECT_EQ(
        page_db_.AddCommitStorageBytes(handler, commit->GetId(), "encoded identifier",
                                       commit->GetRootIdentifier(), commit->GetStorageBytes()),
        Status::OK);
    EXPECT_EQ(page_db_.GetCommitStorageBytes(handler, commit->GetId(), &storage_bytes), Status::OK);
    EXPECT_EQ(commit->GetStorageBytes(), storage_bytes);

    std::vector<CommitId> references;
    EXPECT_EQ(
        page_db_.GetInboundCommitReferences(handler, commit->GetRootIdentifier(), &references),
        Status::OK);
    EXPECT_THAT(references, ElementsAre(commit->GetId()));

    CommitId commit_id;
    EXPECT_EQ(page_db_.GetCommitIdFromRemoteId(handler, "encoded identifier", &commit_id),
              Status::OK);
    EXPECT_EQ(commit_id, commit->GetId());

    EXPECT_EQ(DeleteCommit(handler, commit->GetId(), "encoded identifier",
                           commit->GetRootIdentifier().object_digest()),
              Status::OK);
    EXPECT_EQ(page_db_.GetCommitIdFromRemoteId(handler, "encoded indentifier", &commit_id),
              Status::INTERNAL_NOT_FOUND);
  });
}

TEST_F(PageDbTest, ObjectStorage) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    const ObjectIdentifier object_identifier = RandomObjectIdentifier();
    const ObjectIdentifier child_identifier = RandomObjectIdentifier();
    const std::string content = RandomString(environment_.random(), 32 * 1024);
    std::unique_ptr<const Piece> piece;
    PageDbObjectStatus object_status;

    EXPECT_EQ(page_db_.ReadObject(handler, object_identifier, &piece), Status::INTERNAL_NOT_FOUND);
    ASSERT_EQ(
        page_db_.WriteObject(
            handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create(content)),
            PageDbObjectStatus::TRANSIENT, {{child_identifier.object_digest(), KeyPriority::LAZY}}),
        Status::OK);
    ASSERT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
    EXPECT_EQ(object_status, PageDbObjectStatus::TRANSIENT);
    ASSERT_EQ(page_db_.ReadObject(handler, object_identifier, &piece), Status::OK);
    EXPECT_EQ(piece->GetData(), content);
    ObjectReferencesAndPriority references;
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, child_identifier, &references),
              Status::OK);
    EXPECT_THAT(references,
                ElementsAre(Pair(object_identifier.object_digest(), KeyPriority::LAZY)));
    // Update the object to LOCAL. The new content and references should be
    // ignored.
    const std::string new_content = RandomString(environment_.random(), 32 * 1024);
    ASSERT_EQ(
        page_db_.WriteObject(
            handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create(new_content)),
            PageDbObjectStatus::LOCAL, {{child_identifier.object_digest(), KeyPriority::EAGER}}),
        Status::OK);
    ASSERT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
    EXPECT_EQ(object_status, PageDbObjectStatus::LOCAL);
    EXPECT_EQ(piece->GetData(), content);
    EXPECT_NE(new_content, piece->GetData());
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, child_identifier, &references),
              Status::OK);
    EXPECT_THAT(references,
                ElementsAre(Pair(object_identifier.object_digest(), KeyPriority::LAZY)));
  });
}

TEST_F(PageDbTest, LazyAndEagerReferences) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    const auto object_identifier = RandomObjectIdentifier();
    const ObjectIdentifier child_identifier = RandomObjectIdentifier();

    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL,
                  {{child_identifier.object_digest(), KeyPriority::LAZY},
                   {child_identifier.object_digest(), KeyPriority::EAGER}}),
              Status::OK);

    ObjectReferencesAndPriority references;
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, child_identifier, &references),
              Status::OK);
    EXPECT_THAT(references,
                UnorderedElementsAre(Pair(object_identifier.object_digest(), KeyPriority::LAZY),
                                     Pair(object_identifier.object_digest(), KeyPriority::EAGER)));
  });
}

// Tests object deletion is correct, and possible only when there is no in-memory reference to the
// deleted object.
TEST_F(PageDbTest, DeleteObjectWithLiveReference) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Create an object referencing another one (through both lazy and eager references), but not
    // referenced by anything.
    ObjectIdentifier object_identifier = RandomObjectIdentifier();
    const ObjectDigest object_digest = object_identifier.object_digest();
    const ObjectIdentifier child_identifier = RandomObjectIdentifier();
    const ObjectReferencesAndPriority object_references = {
        {child_identifier.object_digest(), KeyPriority::LAZY},
        {child_identifier.object_digest(), KeyPriority::EAGER}};
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, object_references),
              Status::OK);

    // Check that the object, status and references have been written correctly.
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier), Status::OK);

    PageDbObjectStatus object_status;
    EXPECT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
    EXPECT_EQ(object_status, PageDbObjectStatus::LOCAL);

    ObjectReferencesAndPriority references;
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, child_identifier, &references),
              Status::OK);
    EXPECT_THAT(references, Not(IsEmpty()));

    // First attempt to delete the object. This should fail because |object_identifier| still
    // references it.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, object_references), Status::CANCELED);

    // Discard the live reference.
    object_identifier = ObjectIdentifier();

    // Second attempt to delete the object and its references.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, object_references), Status::OK);

    // Mint a new reference to the object.
    object_identifier =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, object_digest);

    // Check that object, its status and its references are gone.
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier), Status::INTERNAL_NOT_FOUND);
    EXPECT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
    EXPECT_EQ(object_status, PageDbObjectStatus::UNKNOWN);
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, child_identifier, &references),
              Status::OK);
    EXPECT_THAT(references, IsEmpty());
  });
}

// Tests that creating an in-memory reference to an object pending deletion aborts the deletion.
TEST_F(PageDbTest, DeleteObjectAbortedByLiveReference) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Create an object not referenced by anything.
    ObjectIdentifier object_identifier = RandomObjectIdentifier();
    const ObjectDigest object_digest = object_identifier.object_digest();
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, {}),
              Status::OK);

    // Check that the object, status and references have been written correctly.
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier), Status::OK);

    // Attempt to start deletion, fails because the object is live.
    std::unique_ptr<PageDbImpl::Batch> batch;
    ASSERT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest, {}), Status::CANCELED);

    // Discard the live reference.
    object_identifier = ObjectIdentifier();

    // Second attempt to start deletion.
    ASSERT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest, {}), Status::OK);

    // Mint a new reference to the object, which aborts the pending deletion.
    object_identifier =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, object_digest);

    // Check that deletion has been aborted.
    EXPECT_EQ(batch->Execute(handler), Status::CANCELED);
  });
}

// Tests that on-disk references prevent deletion of a transient object, discarding commit-object
// reference first.
TEST_F(PageDbTest, DeleteTransientObjectWithOnDiskReferences) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Create an object referenced by another object and a commit.
    ObjectIdentifier object_identifier = RandomObjectIdentifier();
    const ObjectDigest object_digest = object_identifier.object_digest();
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::TRANSIENT, {}),
              Status::OK);

    ObjectIdentifier parent_identifier = RandomObjectIdentifier();
    const ObjectDigest parent_digest = parent_identifier.object_digest();
    const ObjectReferencesAndPriority parent_references = {{object_digest, KeyPriority::EAGER}};
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(parent_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, parent_references),
              Status::OK);

    const CommitId commit_id = RandomCommitId(environment_.random());
    EXPECT_EQ(page_db_.AddCommitStorageBytes(handler, commit_id, "fake remote id",
                                             object_identifier, "fake storage bytes"),
              Status::OK);

    // Discard the live references.
    object_identifier = ObjectIdentifier();
    parent_identifier = ObjectIdentifier();

    // Deletion should fail because of the on-disk references.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::CANCELED);

    // Discard the commit-object on-disk reference.
    EXPECT_EQ(DeleteCommit(handler, commit_id, "fake remote id", object_digest), Status::OK);

    // Deletion should still fail because of the object-object reference.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::CANCELED);

    // Discard the object-object on-disk reference.
    EXPECT_EQ(page_db_.DeleteObject(handler, parent_digest, parent_references), Status::OK);

    // Deletion now succeeds.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::OK);

    // Mint a new reference to the object.
    object_identifier =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, object_digest);

    // Check that object is gone.
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier), Status::INTERNAL_NOT_FOUND);
  });
}

// Tests that on-disk references prevent deletion of a local object, discarding object-object
// reference first.
TEST_F(PageDbTest, DeleteLocalObjectWithOnDiskReferences) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Create an object referenced by another object and a commit.
    ObjectIdentifier object_identifier = RandomObjectIdentifier();
    const ObjectDigest object_digest = object_identifier.object_digest();
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, {}),
              Status::OK);

    ObjectIdentifier parent_identifier = RandomObjectIdentifier();
    const ObjectDigest parent_digest = parent_identifier.object_digest();
    const ObjectReferencesAndPriority parent_references = {{object_digest, KeyPriority::EAGER}};
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(parent_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, parent_references),
              Status::OK);

    const CommitId commit_id = RandomCommitId(environment_.random());
    EXPECT_EQ(page_db_.AddCommitStorageBytes(handler, commit_id, "fake remote id",
                                             object_identifier, "fake storage bytes"),
              Status::OK);

    // Discard the live references.
    object_identifier = ObjectIdentifier();
    parent_identifier = ObjectIdentifier();

    // Deletion should fail because of the on-disk references.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::CANCELED);

    // Discard the object-object on-disk reference.
    EXPECT_EQ(page_db_.DeleteObject(handler, parent_digest, parent_references), Status::OK);

    // Deletion should still fail because of the commit-object reference.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::CANCELED);

    // Discard the commit-object on-disk reference.
    EXPECT_EQ(DeleteCommit(handler, commit_id, "fake remote id", object_digest), Status::OK);

    // Deletion now succeeds.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::OK);

    // Mint a new reference to the object.
    object_identifier =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, object_digest);

    // Check that object is gone.
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier), Status::INTERNAL_NOT_FOUND);
  });
}

// Tests that object-object on-disk references prevent deletion of a synchronized object.
// Commit-object reference should not prevent deletion.
TEST_F(PageDbTest, DeleteSyncedObjectWithOnDiskReferences) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Create an object referenced by another object and a commit.
    ObjectIdentifier object_identifier = RandomObjectIdentifier();
    const ObjectDigest object_digest = object_identifier.object_digest();
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::SYNCED, {}),
              Status::OK);

    ObjectIdentifier parent_identifier = RandomObjectIdentifier();
    const ObjectDigest parent_digest = parent_identifier.object_digest();
    const ObjectReferencesAndPriority parent_references = {{object_digest, KeyPriority::EAGER}};
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(parent_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, parent_references),
              Status::OK);

    const CommitId commit_id = RandomCommitId(environment_.random());
    EXPECT_EQ(page_db_.AddCommitStorageBytes(handler, commit_id, "fake remote id",
                                             object_identifier, "fake storage bytes"),
              Status::OK);

    // Discard the live references.
    object_identifier = ObjectIdentifier();
    parent_identifier = ObjectIdentifier();

    // Deletion should fail because of the object-object reference.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::CANCELED);

    // Discard the object-object on-disk reference.
    EXPECT_EQ(page_db_.DeleteObject(handler, parent_digest, parent_references), Status::OK);

    // Deletion now succeeds.
    EXPECT_EQ(page_db_.DeleteObject(handler, object_digest, {}), Status::OK);

    // Mint a new reference to the object.
    object_identifier =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, object_digest);

    // Check that object is gone.
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier), Status::INTERNAL_NOT_FOUND);
  });
}

// Tests that all deletions are aborted correctly when several deletions are batched together and
// one of them fails.
TEST_F(PageDbTest, DeleteObjectBatchAbort) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Create two objects not referenced by anything.
    ObjectIdentifier object_identifier1 = RandomObjectIdentifier();
    const ObjectDigest object_digest1 = object_identifier1.object_digest();
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier1, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, {}),
              Status::OK);

    ObjectIdentifier object_identifier2 = RandomObjectIdentifier();
    const ObjectDigest object_digest2 = object_identifier2.object_digest();
    ASSERT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier2, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, {}),
              Status::OK);

    // Check that the objects have been written correctly.
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier1), Status::OK);
    EXPECT_EQ(page_db_.HasObject(handler, object_identifier2), Status::OK);

    // Discard the live references.
    object_identifier1 = ObjectIdentifier();
    object_identifier2 = ObjectIdentifier();

    // Start deleting both objects.
    std::unique_ptr<PageDbImpl::Batch> batch;
    ASSERT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest1, {}), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest2, {}), Status::OK);

    // Mint a new reference to the first object, which aborts the pending deletion.
    page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, object_digest1);

    // Check that the whole batch has been aborted.
    EXPECT_EQ(batch->Execute(handler), Status::CANCELED);

    // Check that both deletions have stopped been tracked: it should be possible to restart them
    // immediately.
    ASSERT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest1, {}), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest2, {}), Status::OK);

    // Drop the batch.
    batch.reset();

    // Check that both deletions have stopped been tracked when the batch was dropped: it should be
    // possible to restart them immediately again.
    ASSERT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest1, {}), Status::OK);
    EXPECT_EQ(batch->DeleteObject(handler, object_digest2, {}), Status::OK);
  });
}

TEST_F(PageDbTest, UnsyncedCommits) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_id = RandomCommitId(environment_.random());
    std::vector<CommitId> commit_ids;
    EXPECT_EQ(page_db_.GetUnsyncedCommitIds(handler, &commit_ids), Status::OK);
    EXPECT_TRUE(commit_ids.empty());

    EXPECT_EQ(page_db_.MarkCommitIdUnsynced(handler, commit_id, 0), Status::OK);
    EXPECT_EQ(page_db_.GetUnsyncedCommitIds(handler, &commit_ids), Status::OK);
    EXPECT_EQ(commit_ids.size(), 1u);
    EXPECT_EQ(commit_ids[0], commit_id);
    bool is_synced;
    EXPECT_EQ(page_db_.IsCommitSynced(handler, commit_id, &is_synced), Status::OK);
    EXPECT_FALSE(is_synced);

    EXPECT_EQ(page_db_.MarkCommitIdSynced(handler, commit_id), Status::OK);
    EXPECT_EQ(page_db_.GetUnsyncedCommitIds(handler, &commit_ids), Status::OK);
    EXPECT_TRUE(commit_ids.empty());
    EXPECT_EQ(page_db_.IsCommitSynced(handler, commit_id, &is_synced), Status::OK);
    EXPECT_TRUE(is_synced);
  });
}

TEST_F(PageDbTest, OrderUnsyncedCommitsByTimestamp) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    CommitId commit_ids[] = {RandomCommitId(environment_.random()),
                             RandomCommitId(environment_.random()),
                             RandomCommitId(environment_.random())};
    // Add three unsynced commits with timestamps 200, 300 and 100.
    EXPECT_EQ(page_db_.MarkCommitIdUnsynced(handler, commit_ids[0], 200), Status::OK);
    EXPECT_EQ(page_db_.MarkCommitIdUnsynced(handler, commit_ids[1], 300), Status::OK);
    EXPECT_EQ(page_db_.MarkCommitIdUnsynced(handler, commit_ids[2], 100), Status::OK);

    // The result should be ordered by the given timestamps.
    std::vector<CommitId> found_ids;
    EXPECT_EQ(page_db_.GetUnsyncedCommitIds(handler, &found_ids), Status::OK);
    EXPECT_EQ(found_ids.size(), 3u);
    EXPECT_EQ(commit_ids[2], found_ids[0]);
    EXPECT_EQ(commit_ids[0], found_ids[1]);
    EXPECT_EQ(commit_ids[1], found_ids[2]);
  });
}

TEST_F(PageDbTest, UnsyncedPieces) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    auto object_identifier = RandomObjectIdentifier();
    std::vector<ObjectIdentifier> object_identifiers;
    EXPECT_EQ(page_db_.GetUnsyncedPieces(handler, &object_identifiers), Status::OK);
    EXPECT_TRUE(object_identifiers.empty());

    EXPECT_EQ(page_db_.WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL, {}),
              Status::OK);
    EXPECT_EQ(page_db_.SetObjectStatus(handler, object_identifier, PageDbObjectStatus::LOCAL),
              Status::OK);
    EXPECT_EQ(page_db_.GetUnsyncedPieces(handler, &object_identifiers), Status::OK);
    EXPECT_EQ(object_identifiers.size(), 1u);
    EXPECT_EQ(object_identifiers[0], object_identifier);
    PageDbObjectStatus object_status;
    EXPECT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
    EXPECT_EQ(object_status, PageDbObjectStatus::LOCAL);

    EXPECT_EQ(page_db_.SetObjectStatus(handler, object_identifier, PageDbObjectStatus::SYNCED),
              Status::OK);
    EXPECT_EQ(page_db_.GetUnsyncedPieces(handler, &object_identifiers), Status::OK);
    EXPECT_TRUE(object_identifiers.empty());
    EXPECT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
    EXPECT_EQ(object_status, PageDbObjectStatus::SYNCED);
  });
}

TEST_F(PageDbTest, Batch) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::unique_ptr<PageDb::Batch> batch;
    ASSERT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    ASSERT_TRUE(batch);

    auto object_identifier = RandomObjectIdentifier();
    auto eager_identifier = RandomObjectIdentifier();
    auto lazy_identifier = RandomObjectIdentifier();
    EXPECT_EQ(batch->WriteObject(
                  handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                  PageDbObjectStatus::LOCAL,
                  {{eager_identifier.object_digest(), KeyPriority::EAGER},
                   {lazy_identifier.object_digest(), KeyPriority::LAZY}}),
              Status::OK);

    // Check that we don't have any unsynced piece nor reference prior to
    // executing the batch.
    std::vector<ObjectIdentifier> object_identifiers;
    EXPECT_EQ(page_db_.GetUnsyncedPieces(handler, &object_identifiers), Status::OK);
    EXPECT_THAT(object_identifiers, IsEmpty());
    ObjectReferencesAndPriority references;
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, eager_identifier, &references),
              Status::OK);
    EXPECT_THAT(references, IsEmpty());
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, lazy_identifier, &references),
              Status::OK);
    EXPECT_THAT(references, IsEmpty());

    // Execute the batch write.
    EXPECT_EQ(batch->Execute(handler), Status::OK);

    // Check unsynced status of written pieces.
    EXPECT_EQ(page_db_.GetUnsyncedPieces(handler, &object_identifiers), Status::OK);
    EXPECT_THAT(object_identifiers, ElementsAre(object_identifier));
    // Check the eager reference.
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, eager_identifier, &references),
              Status::OK);
    EXPECT_THAT(references,
                ElementsAre(Pair(object_identifier.object_digest(), KeyPriority::EAGER)));
    // Check the lazy reference.
    EXPECT_EQ(page_db_.GetInboundObjectReferences(handler, lazy_identifier, &references),
              Status::OK);
    EXPECT_THAT(references,
                ElementsAre(Pair(object_identifier.object_digest(), KeyPriority::LAZY)));
  });
}

TEST_F(PageDbTest, PageDbObjectStatus) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    PageDbObjectStatus initial_statuses[] = {PageDbObjectStatus::TRANSIENT,
                                             PageDbObjectStatus::LOCAL, PageDbObjectStatus::SYNCED};
    PageDbObjectStatus next_statuses[] = {PageDbObjectStatus::LOCAL, PageDbObjectStatus::SYNCED};
    for (auto initial_status : initial_statuses) {
      for (auto next_status : next_statuses) {
        auto object_identifier = RandomObjectIdentifier();
        PageDbObjectStatus object_status;
        ASSERT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
        EXPECT_EQ(object_status, PageDbObjectStatus::UNKNOWN);
        ASSERT_EQ(page_db_.WriteObject(
                      handler, DataChunkPiece(object_identifier, DataSource::DataChunk::Create("")),
                      initial_status, {}),
                  Status::OK);
        ASSERT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
        EXPECT_EQ(object_status, initial_status);
        ASSERT_EQ(page_db_.SetObjectStatus(handler, object_identifier, next_status), Status::OK);

        PageDbObjectStatus expected_status = std::max(initial_status, next_status);
        ASSERT_EQ(page_db_.GetObjectStatus(handler, object_identifier, &object_status), Status::OK);
        EXPECT_EQ(object_status, expected_status);
      }
    }
  });
}

TEST_F(PageDbTest, GetObjectStatusKeys) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    // Create 3 distinct object identifiers with 3 different statuses, sharing the same
    // object digest.
    const ObjectDigest object_digest = RandomObjectDigest(environment_.random());
    const ObjectIdentifier identifier_1 =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(1u, object_digest);
    ASSERT_EQ(page_db_.WriteObject(handler,
                                   DataChunkPiece(identifier_1, DataSource::DataChunk::Create("")),
                                   PageDbObjectStatus::TRANSIENT, {}),
              Status::OK);
    const ObjectIdentifier identifier_2 =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(2u, object_digest);
    ASSERT_EQ(page_db_.WriteObject(handler,
                                   DataChunkPiece(identifier_2, DataSource::DataChunk::Create("")),
                                   PageDbObjectStatus::LOCAL, {}),
              Status::OK);
    const ObjectIdentifier identifier_3 =
        page_storage_.GetObjectIdentifierFactory()->MakeObjectIdentifier(3u, object_digest);
    ASSERT_EQ(page_db_.WriteObject(handler,
                                   DataChunkPiece(identifier_3, DataSource::DataChunk::Create("")),
                                   PageDbObjectStatus::SYNCED, {}),
              Status::OK);

    std::map<std::string, PageDbObjectStatus> keys;
    ASSERT_EQ(page_db_.GetObjectStatusKeys(handler, object_digest, &keys), Status::OK);

    EXPECT_THAT(keys,
                UnorderedElementsAre(
                    Pair(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::TRANSIENT, identifier_1),
                         PageDbObjectStatus::TRANSIENT),
                    Pair(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::LOCAL, identifier_2),
                         PageDbObjectStatus::LOCAL),
                    Pair(ObjectStatusRow::GetKeyFor(PageDbObjectStatus::SYNCED, identifier_3),
                         PageDbObjectStatus::SYNCED)));
  });
}

TEST_F(PageDbTest, SyncMetadata) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    std::vector<std::pair<fxl::StringView, fxl::StringView>> keys_and_values = {{"foo1", "foo2"},
                                                                                {"bar1", " bar2 "}};
    for (const auto& key_and_value : keys_and_values) {
      auto key = key_and_value.first;
      auto value = key_and_value.second;
      std::string returned_value;
      EXPECT_EQ(page_db_.GetSyncMetadata(handler, key, &returned_value),
                Status::INTERNAL_NOT_FOUND);

      EXPECT_EQ(page_db_.SetSyncMetadata(handler, key, value), Status::OK);
      EXPECT_EQ(page_db_.GetSyncMetadata(handler, key, &returned_value), Status::OK);
      EXPECT_EQ(returned_value, value);
    }
  });
}

TEST_F(PageDbTest, PageIsOnline) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    bool page_is_online;

    // Check that the initial state is not online.
    ASSERT_EQ(page_db_.IsPageOnline(handler, &page_is_online), Status::OK);
    EXPECT_FALSE(page_is_online);

    // Mark page as online and check it was updated.
    EXPECT_EQ(page_db_.MarkPageOnline(handler), Status::OK);
    ASSERT_EQ(page_db_.IsPageOnline(handler, &page_is_online), Status::OK);
    EXPECT_TRUE(page_is_online);
  });
}

// This test reproduces the crash of LE-451. The crash is due to a subtle
// ordering of coroutine execution that is exactly reproduced here.
TEST_F(PageDbTest, LE_451_ReproductionTest) {
  auto id = RandomObjectIdentifier();
  RunInCoroutine([&](CoroutineHandler* handler) {
    EXPECT_EQ(page_db_.WriteObject(handler, DataChunkPiece(id, DataSource::DataChunk::Create("")),
                                   PageDbObjectStatus::LOCAL, {}),
              Status::OK);
  });
  CoroutineHandler* handler1 = nullptr;
  CoroutineHandler* handler2 = nullptr;
  environment_.coroutine_service()->StartCoroutine([&](CoroutineHandler* handler) {
    handler1 = handler;
    std::unique_ptr<PageDb::Batch> batch;
    EXPECT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    EXPECT_EQ(batch->SetObjectStatus(handler, id, PageDbObjectStatus::SYNCED), Status::OK);
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }
    EXPECT_EQ(batch->Execute(handler), Status::OK);
    handler1 = nullptr;
  });
  environment_.coroutine_service()->StartCoroutine([&](CoroutineHandler* handler) {
    handler2 = handler;
    std::unique_ptr<PageDb::Batch> batch;
    EXPECT_EQ(page_db_.StartBatch(handler, &batch), Status::OK);
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
      return;
    }
    EXPECT_EQ(batch->SetObjectStatus(handler, id, PageDbObjectStatus::LOCAL), Status::OK);
    EXPECT_EQ(batch->Execute(handler), Status::OK);
    handler2 = nullptr;
  });
  ASSERT_TRUE(handler1);
  ASSERT_TRUE(handler2);

  // Reach the 2 yield points.
  RunLoopUntilIdle();

  // Posting a task at this level ensures that the right interleaving between
  // reading and writing object status happens.
  async::PostTask(dispatcher(), [&] { handler1->Resume(coroutine::ContinuationStatus::OK); });
  handler2->Resume(coroutine::ContinuationStatus::OK);

  // Finish the test.
  RunLoopUntilIdle();

  // Ensures both coroutines are terminated.
  ASSERT_FALSE(handler1);
  ASSERT_FALSE(handler2);
}

TEST_F(PageDbTest, DeviceId) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    clocks::DeviceId device_id;
    EXPECT_EQ(page_db_.GetDeviceId(handler, &device_id), Status::INTERNAL_NOT_FOUND);

    device_id = clocks::DeviceId{"device_id", 2};
    EXPECT_EQ(page_db_.SetDeviceId(handler, device_id), Status::OK);

    clocks::DeviceId actual_device_id;
    EXPECT_EQ(page_db_.GetDeviceId(handler, &actual_device_id), Status::OK);

    EXPECT_EQ(actual_device_id, device_id);
  });
}

class FakeDbInterruptedHasKey : public storage::fake::FakeDb {
 public:
  FakeDbInterruptedHasKey(async_dispatcher_t* dispatcher) : FakeDb(dispatcher) {}

  Status HasKey(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override {
    if (handler->Yield() == coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    return Status::OK;
  }
};

TEST_F(PageDbTest, SetDeviceIdInterrupted) {
#ifndef NDEBUG
  PageDbImpl page_db(&environment_, page_storage_.GetObjectIdentifierFactory(),
                     std::make_unique<FakeDbInterruptedHasKey>(dispatcher()));

  CoroutineHandler* handler_ptr = nullptr;
  environment_.coroutine_service()->StartCoroutine([&](CoroutineHandler* handler) {
    handler_ptr = handler;
    // On debug mode, |SetDeviceId| is interrupted because of the call to |Db::HasKey|.
    EXPECT_EQ(page_db.SetDeviceId(handler, clocks::DeviceId{"device_id", 0}), Status::INTERRUPTED);

    handler_ptr = nullptr;
  });
  ASSERT_TRUE(handler_ptr);

  // Reach the yield point.
  RunLoopUntilIdle();

  handler_ptr->Resume(coroutine::ContinuationStatus::INTERRUPTED);

  // Finish the test.
  RunLoopUntilIdle();

  // Ensures that the coroutine has terminated.
  ASSERT_FALSE(handler_ptr);
#endif
}

TEST_F(PageDbTest, GetClock) {
  RunInCoroutine([&](CoroutineHandler* handler) {
    Clock clock;
    // No clock at the beginning.
    EXPECT_EQ(page_db_.GetClock(handler, &clock), Status::INTERNAL_NOT_FOUND);
    // Set an empty clock and retrieve it.
    EXPECT_EQ(page_db_.SetClock(handler, clock), Status::OK);
    EXPECT_EQ(page_db_.GetClock(handler, &clock), Status::OK);
    EXPECT_THAT(clock, IsEmpty());

    clock.emplace(clocks::DeviceId{"device_id_1", 0},
                  DeviceEntry{ClockEntry{RandomCommitId(environment_.random()), 1},
                              std::make_optional<ClockEntry>(
                                  ClockEntry{RandomCommitId(environment_.random()), 2})});
    clock.emplace(clocks::DeviceId{"device_id_2", 0},
                  DeviceEntry{ClockEntry{RandomCommitId(environment_.random()), 3}, std::nullopt});
    clock.emplace(clocks::DeviceId{"device_id_3", 0}, ClockTombstone());
    EXPECT_EQ(page_db_.SetClock(handler, clock), Status::OK);

    Clock actual_clock;
    EXPECT_EQ(page_db_.GetClock(handler, &actual_clock), Status::OK);

    EXPECT_EQ(actual_clock, clock);
  });
}

}  // namespace
}  // namespace storage
