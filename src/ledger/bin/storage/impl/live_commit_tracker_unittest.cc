// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/live_commit_tracker.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {
namespace {

using ::testing::Not;
using ::testing::ResultOf;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

std::vector<CommitId> ToCommitIdVector(const std::vector<std::unique_ptr<const Commit>>& commits) {
  std::vector<CommitId> ids;
  for (const auto& commit : commits) {
    ids.push_back(commit->GetId());
  }
  return ids;
};

std::set<CommitId> ToCommitIdSet(const std::vector<std::unique_ptr<const Commit>>& commits) {
  std::set<CommitId> ids;
  for (const auto& commit : commits) {
    ids.insert(commit->GetId());
  }
  return ids;
};

class LiveCommitTrackerTest : public ledger::TestWithEnvironment {
 public:
  LiveCommitTrackerTest() : encryption_service_(dispatcher()) {}

  ~LiveCommitTrackerTest() override {}

  // Test:
  void SetUp() override {
    tmpfs_ = std::make_unique<scoped_tmpfs::ScopedTmpFS>();
    auto leveldb = std::make_unique<fake::FakeDb>(dispatcher());
    PageId id = RandomString(environment_.random(), 10);
    storage_ = std::make_unique<PageStorageImpl>(
        &environment_, &encryption_service_, std::move(leveldb), id, CommitPruningPolicy::NEVER);

    bool called;
    Status status;
    storage_->Init(callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    ASSERT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(id, storage_->GetId());
  }

  // Returns the first head commit from PageStorage.
  std::unique_ptr<const Commit> GetFirstHead() {
    std::vector<std::unique_ptr<const Commit>> heads = GetHeads();
    EXPECT_FALSE(heads.empty());
    return std::move(heads[0]);
  }

  // Returns the list of commits from PageStorage.
  std::vector<std::unique_ptr<const Commit>> GetHeads() {
    std::vector<std::unique_ptr<const Commit>> heads;
    Status status = storage_->GetHeadCommits(&heads);
    EXPECT_EQ(Status::OK, status);
    return heads;
  }

  // Returns a randomly created new commit, child of |base|.
  std::unique_ptr<const Commit> CreateRandomCommit(std::unique_ptr<const Commit> base) {
    std::unique_ptr<Journal> journal = storage_->StartCommit(std::move(base));
    journal->Put("key", RandomObjectIdentifier(environment_.random()), KeyPriority::EAGER);
    bool called;
    Status status;
    std::unique_ptr<const Commit> commit;
    storage_->CommitJournal(std::move(journal),
                            callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(Status::OK, status);
    return commit;
  }

 protected:
  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> tmpfs_;
  encryption::FakeEncryptionService encryption_service_;
  std::unique_ptr<PageStorageImpl> storage_;
};

TEST_F(LiveCommitTrackerTest, GetHeads) {
  LiveCommitTracker* tracker = storage_->GetCommitTracker();

  auto initial_heads = GetHeads();
  EXPECT_THAT(ToCommitIdVector(tracker->GetHeads()),
              UnorderedElementsAreArray(ToCommitIdVector(initial_heads)));

  CreateRandomCommit(GetFirstHead());

  // Heads have changed;
  EXPECT_THAT(ToCommitIdVector(tracker->GetHeads()),
              Not(UnorderedElementsAreArray(ToCommitIdVector(initial_heads))));
  EXPECT_THAT(ToCommitIdVector(tracker->GetHeads()),
              UnorderedElementsAreArray(ToCommitIdVector(GetHeads())));
}

// Tests that GetLiveCommits returns indeed a list of live commits. Registration
// and unregistration are tested indirectly through their use by Commit and
// Journal objects.
TEST_F(LiveCommitTrackerTest, GetLiveCommits) {
  LiveCommitTracker* tracker = storage_->GetCommitTracker();

  // When no journal has started, live commits should be the heads.
  auto initial_heads = ToCommitIdSet(GetHeads());
  EXPECT_THAT(ToCommitIdSet(tracker->GetLiveCommits()), UnorderedElementsAreArray(initial_heads));

  // Let's keep an old commit, and make new ones.
  std::unique_ptr<const Commit> old_commit = GetFirstHead();
  CommitId old_id = old_commit->GetId();

  // Create two chained commits. The head should only contain the new commit.
  std::unique_ptr<const Commit> new_commit =
      CreateRandomCommit(CreateRandomCommit(old_commit->Clone()));
  CommitId new_id = new_commit->GetId();
  new_commit.reset();
  EXPECT_THAT(ToCommitIdVector(GetHeads()), UnorderedElementsAre(new_id));

  // Even if we don't keep hold of the new commit, it should remain live as it
  // is a head.
  EXPECT_THAT(ToCommitIdSet(tracker->GetLiveCommits()), UnorderedElementsAre(old_id, new_id));

  // If we use old_commit in a journal, it remains live even if we don't hold it
  // anymore.
  std::unique_ptr<Journal> journal = storage_->StartCommit(std::move(old_commit));
  EXPECT_THAT(ToCommitIdSet(tracker->GetLiveCommits()), UnorderedElementsAre(old_id, new_id));

  // If we don't hold neither old_commit nor a journal based on it, it is no
  // longer live.
  journal.reset();
  EXPECT_THAT(ToCommitIdSet(tracker->GetLiveCommits()), UnorderedElementsAre(new_id));
}

}  // namespace
}  // namespace storage
