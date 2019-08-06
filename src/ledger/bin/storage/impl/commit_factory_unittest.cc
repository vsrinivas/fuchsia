// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_factory.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>

#include <memory>
#include <tuple>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/macros.h"

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
}

std::set<CommitId> ToCommitIdSet(const std::vector<std::unique_ptr<const Commit>>& commits) {
  std::set<CommitId> ids;
  for (const auto& commit : commits) {
    ids.insert(commit->GetId());
  }
  return ids;
}

class CommitFactoryTest : public ledger::TestWithEnvironment {
 public:
  CommitFactoryTest() : encryption_service_(dispatcher()) {}

  ~CommitFactoryTest() override {}

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
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(storage_->GetId(), id);
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
    EXPECT_EQ(status, Status::OK);
    return heads;
  }

  // Returns a randomly created new commit, child of |base|.
  std::unique_ptr<const Commit> CreateRandomCommit(std::unique_ptr<const Commit> base) {
    std::unique_ptr<Journal> journal = storage_->StartCommit(std::move(base));
    journal->Put(
        "key",
        RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory()),
        KeyPriority::EAGER);
    bool called;
    Status status;
    std::unique_ptr<const Commit> commit;
    storage_->CommitJournal(std::move(journal),
                            callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    return commit;
  }

  PageStorage* GetStorage() { return storage_.get(); }

  bool CheckCommitEquals(const Commit& expected, const Commit& commit) {
    return std::forward_as_tuple(expected.GetId(), expected.GetTimestamp(), expected.GetParentIds(),
                                 expected.GetRootIdentifier()) ==
           std::forward_as_tuple(commit.GetId(), commit.GetTimestamp(), commit.GetParentIds(),
                                 commit.GetRootIdentifier());
  }

  bool CheckCommitStorageBytes(const std::unique_ptr<const Commit>& commit) {
    std::unique_ptr<const Commit> copy;
    Status status = storage_->GetCommitFactory()->FromStorageBytes(
        commit->GetId(), commit->GetStorageBytes().ToString(), &copy);
    EXPECT_EQ(status, Status::OK);

    return CheckCommitEquals(*commit, *copy);
  }

 protected:
  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> tmpfs_;
  encryption::FakeEncryptionService encryption_service_;
  std::unique_ptr<PageStorageImpl> storage_;
};

TEST_F(CommitFactoryTest, CommitStorageBytes) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::vector<std::unique_ptr<const Commit>> parents;

  // A commit with one parent.
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit));

  // A commit with two parents.
  parents = std::vector<std::unique_ptr<const Commit>>();
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  std::unique_ptr<const Commit> commit2 = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit2));
}

TEST_F(CommitFactoryTest, CloneCommit) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));
  std::unique_ptr<const Commit> copy;
  Status status = storage_->GetCommitFactory()->FromStorageBytes(
      commit->GetId(), commit->GetStorageBytes().ToString(), &copy);
  ASSERT_EQ(status, Status::OK);
  std::unique_ptr<const Commit> clone = commit->Clone();
  EXPECT_TRUE(CheckCommitEquals(*copy, *clone));
}

TEST_F(CommitFactoryTest, MergeCommitTimestamp) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  EXPECT_NE(parents[0]->GetTimestamp(), parents[1]->GetTimestamp());
  auto max_timestamp = std::max(parents[0]->GetTimestamp(), parents[1]->GetTimestamp());
  std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));

  EXPECT_EQ(commit->GetTimestamp(), max_timestamp);
}

TEST_F(CommitFactoryTest, IsAlive) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::vector<std::unique_ptr<const Commit>> parents;

  // A commit with one parent.
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));
  EXPECT_TRUE(commit->IsAlive());

  storage_.reset();

  EXPECT_FALSE(commit->IsAlive());
}

TEST_F(CommitFactoryTest, GetHeads) {
  CommitFactory* factory = storage_->GetCommitFactory();

  auto initial_heads = GetHeads();
  EXPECT_THAT(ToCommitIdVector(factory->GetHeads()),
              UnorderedElementsAreArray(ToCommitIdVector(initial_heads)));

  CreateRandomCommit(GetFirstHead());

  // Heads have changed;
  EXPECT_THAT(ToCommitIdVector(factory->GetHeads()),
              Not(UnorderedElementsAreArray(ToCommitIdVector(initial_heads))));
  EXPECT_THAT(ToCommitIdVector(factory->GetHeads()),
              UnorderedElementsAreArray(ToCommitIdVector(GetHeads())));
}

// Tests that GetLiveCommits returns indeed a list of live commits. Registration
// and unregistration are tested indirectly through their use by Commit and
// Journal objects.
TEST_F(CommitFactoryTest, GetLiveCommits) {
  CommitFactory* factory = storage_->GetCommitFactory();

  // When no journal has started, live commits should be the heads.
  auto initial_heads = ToCommitIdSet(GetHeads());
  EXPECT_THAT(ToCommitIdSet(factory->GetLiveCommits()), UnorderedElementsAreArray(initial_heads));

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
  EXPECT_THAT(ToCommitIdSet(factory->GetLiveCommits()), UnorderedElementsAre(old_id, new_id));

  // If we use old_commit in a journal, it remains live even if we don't hold it
  // anymore.
  std::unique_ptr<Journal> journal = storage_->StartCommit(std::move(old_commit));
  EXPECT_THAT(ToCommitIdSet(factory->GetLiveCommits()), UnorderedElementsAre(old_id, new_id));

  // If we don't hold neither old_commit nor a journal based on it, it is no
  // longer live.
  journal.reset();
  EXPECT_THAT(ToCommitIdSet(factory->GetLiveCommits()), UnorderedElementsAre(new_id));
}

}  // namespace
}  // namespace storage
