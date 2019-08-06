// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/page_db.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/set_when_called.h>
#include <lib/zx/time.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/impl/commit_factory.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/object_impl.h"
#include "src/ledger/bin/storage/impl/page_db_impl.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/macros.h"

namespace storage {
namespace {

using ::coroutine::CoroutineHandler;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

std::unique_ptr<LevelDb> GetLevelDb(async_dispatcher_t* dispatcher, ledger::DetachedPath db_path) {
  auto db = std::make_unique<LevelDb>(dispatcher, std::move(db_path));
  EXPECT_EQ(db->Init(), Status::OK);
  return db;
}

class PageDbTest : public ledger::TestWithEnvironment {
 public:
  PageDbTest()
      : encryption_service_(dispatcher()),
        base_path(tmpfs_.root_fd()),
        page_storage_(&environment_, &encryption_service_,
                      GetLevelDb(dispatcher(), base_path.SubPath("storage")), "page_id",
                      CommitPruningPolicy::NEVER),
        page_db_(&environment_, page_storage_.GetObjectIdentifierFactory(),
                 GetLevelDb(dispatcher(), base_path.SubPath("page_db"))) {}

  ~PageDbTest() override {}

  // Test:
  void SetUp() override {
    Status status;
    bool called;
    page_storage_.Init(callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    ASSERT_EQ(status, Status::OK);
  }

  ObjectIdentifier RandomObjectIdentifier() {
    return storage::RandomObjectIdentifier(environment_.random(),
                                           page_storage_.GetObjectIdentifierFactory());
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
        environment_.clock(), RandomObjectIdentifier(), std::move(parents));

    std::string storage_bytes;
    EXPECT_EQ(page_db_.GetCommitStorageBytes(handler, commit->GetId(), &storage_bytes),
              Status::INTERNAL_NOT_FOUND);

    EXPECT_EQ(page_db_.AddCommitStorageBytes(handler, commit->GetId(), commit->GetRootIdentifier(),
                                             commit->GetStorageBytes()),
              Status::OK);
    EXPECT_EQ(page_db_.GetCommitStorageBytes(handler, commit->GetId(), &storage_bytes), Status::OK);
    EXPECT_EQ(commit->GetStorageBytes(), storage_bytes);

    std::vector<CommitId> references;
    EXPECT_EQ(
        page_db_.GetInboundCommitReferences(handler, commit->GetRootIdentifier(), &references),
        Status::OK);
    EXPECT_THAT(references, ElementsAre(commit->GetId()));
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

}  // namespace
}  // namespace storage
