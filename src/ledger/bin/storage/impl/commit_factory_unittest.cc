// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_factory.h"

#include <memory>
#include <tuple>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/storage/fake/fake_db.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/leveldb.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace storage {
namespace {

using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;
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

  ~CommitFactoryTest() override = default;

  // Test:
  void SetUp() override {
    tmpfs_ = std::make_unique<scoped_tmpfs::ScopedTmpFS>();
    auto leveldb = std::make_unique<fake::FakeDb>(dispatcher());
    PageId id = RandomString(environment_.random(), 10);
    storage_ = std::make_unique<PageStorageImpl>(
        &environment_, &encryption_service_, std::move(leveldb), id, CommitPruningPolicy::NEVER);

    bool called;
    Status status;
    clocks::DeviceIdManagerEmptyImpl device_id_manager;
    storage_->Init(&device_id_manager,
                   callback::Capture(callback::SetWhenCalled(&called), &status));
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

  // Returns a randomly created merge commit, child of |base| and |other|.
  std::unique_ptr<const Commit> CreateRandomMergeCommit(std::unique_ptr<const Commit> base,
                                                        std::unique_ptr<const Commit> other) {
    std::unique_ptr<Journal> journal =
        storage_->StartMergeCommit(std::move(base), std::move(other));
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
        commit->GetId(), convert::ToString(commit->GetStorageBytes()), &copy);
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
      environment_.clock(), environment_.random(), root_node_identifier, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit));

  // A commit with two parents.
  parents = std::vector<std::unique_ptr<const Commit>>();
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  std::unique_ptr<const Commit> commit2 = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_node_identifier, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit2));
}

TEST_F(CommitFactoryTest, CloneCommit) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_node_identifier, std::move(parents));
  std::unique_ptr<const Commit> copy;
  Status status = storage_->GetCommitFactory()->FromStorageBytes(
      commit->GetId(), convert::ToString(commit->GetStorageBytes()), &copy);
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
      environment_.clock(), environment_.random(), root_node_identifier, std::move(parents));

  EXPECT_EQ(commit->GetTimestamp(), max_timestamp);
}

// Tests that two merges with the same content and parents have the same id.
TEST_F(CommitFactoryTest, MergesAreConsistent) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::unique_ptr<const Commit> parent1 = std::make_unique<CommitRandomImpl>(
      environment_.random(), storage_->GetObjectIdentifierFactory());
  std::unique_ptr<const Commit> parent2 = std::make_unique<CommitRandomImpl>(
      environment_.random(), storage_->GetObjectIdentifierFactory());

  auto make_commit = [&] {
    std::vector<std::unique_ptr<const Commit>> parents;
    parents.emplace_back(parent1->Clone());
    parents.emplace_back(parent2->Clone());
    return storage_->GetCommitFactory()->FromContentAndParents(
        environment_.clock(), environment_.random(), root_node_identifier, std::move(parents));
  };
  auto commit1 = make_commit();
  auto commit2 = make_commit();
  EXPECT_EQ(commit1->GetId(), commit2->GetId());
}

// Tests that two non-merges with the same content and parents have different ids.
TEST_F(CommitFactoryTest, ChangesAreUnique) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::unique_ptr<const Commit> parent = std::make_unique<CommitRandomImpl>(
      environment_.random(), storage_->GetObjectIdentifierFactory());

  auto make_commit = [&] {
    std::vector<std::unique_ptr<const Commit>> parents;
    parents.emplace_back(parent->Clone());
    return storage_->GetCommitFactory()->FromContentAndParents(
        environment_.clock(), environment_.random(), root_node_identifier, std::move(parents));
  };
  auto commit1 = make_commit();
  auto commit2 = make_commit();
  EXPECT_NE(commit1->GetId(), commit2->GetId());
}

TEST_F(CommitFactoryTest, IsAlive) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random(), storage_->GetObjectIdentifierFactory());

  std::vector<std::unique_ptr<const Commit>> parents;

  // A commit with one parent.
  parents.emplace_back(std::make_unique<CommitRandomImpl>(environment_.random(),
                                                          storage_->GetObjectIdentifierFactory()));
  std::unique_ptr<const Commit> commit = storage_->GetCommitFactory()->FromContentAndParents(
      environment_.clock(), environment_.random(), root_node_identifier, std::move(parents));
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

// Tests that GetLiveRootIdentifiers returns the correct set of root identifiers. During this test
// the following commit graph is created:
//
//          -> commit2 -> commit3
//        /
// commit1 -> commit4
TEST_F(CommitFactoryTest, GetLiveRootIdentifiers) {
  CommitFactory* factory = storage_->GetCommitFactory();

  std::unique_ptr<const Commit> commit1 = GetFirstHead();
  ObjectIdentifier commit1_root = commit1->GetRootIdentifier();

  EXPECT_THAT(factory->GetLiveRootIdentifiers(), IsEmpty());

  // Add a commit and expect its root and that of its parent (commit1) to be found in
  // GetLiveRootIdentifiers
  std::unique_ptr<const Commit> commit2 = CreateRandomCommit(commit1->Clone());
  ObjectIdentifier commit2_root = commit2->GetRootIdentifier();
  EXPECT_THAT(factory->GetLiveRootIdentifiers(), UnorderedElementsAre(commit1_root, commit2_root));

  // Add another commit as child of commit2.
  std::unique_ptr<const Commit> commit3 = CreateRandomCommit(commit2->Clone());
  ObjectIdentifier commit3_root = commit3->GetRootIdentifier();
  EXPECT_THAT(factory->GetLiveRootIdentifiers(),
              UnorderedElementsAre(commit1_root, commit2_root, commit3_root));

  // Add another commit as child of commit1.
  std::unique_ptr<const Commit> commit4 = CreateRandomCommit(commit1->Clone());
  ObjectIdentifier commit4_root = commit4->GetRootIdentifier();
  EXPECT_THAT(factory->GetLiveRootIdentifiers(),
              UnorderedElementsAre(commit1_root, commit2_root, commit3_root, commit4_root));

  // Mark commit2 as synced. Nothing should change: commit2_root is also a dependency for commit3,
  // and commit1_root is a dependency for commit4.
  bool called;
  Status status;
  storage_->MarkCommitSynced(commit2->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(factory->GetLiveRootIdentifiers(),
              UnorderedElementsAre(commit1_root, commit2_root, commit3_root, commit4_root));

  // Mark commit4 as synced: both commit4_root and its parent's root (commit1_root) should be
  // removed.
  storage_->MarkCommitSynced(commit4->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(factory->GetLiveRootIdentifiers(), UnorderedElementsAre(commit2_root, commit3_root));

  // Mark commit3 as synced. Now that all commits are synced the set should be empty.
  storage_->MarkCommitSynced(commit3->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(factory->GetLiveRootIdentifiers(), IsEmpty());
}

// Tests that GetLiveRootIdentifiers returns the correct set of root identifiers. During this test
// the following commit graph is created:
//
//          -> commit2 ---> mergeCommit
//        /             /
// commit1 --> commit3 /
TEST_F(CommitFactoryTest, GetLiveRootIdentifiersOnMergeCommit) {
  CommitFactory* factory = storage_->GetCommitFactory();

  std::unique_ptr<const Commit> commit1 = GetFirstHead();
  ObjectIdentifier commit1_root = commit1->GetRootIdentifier();

  EXPECT_THAT(factory->GetLiveRootIdentifiers(), IsEmpty());

  // Add a commit and expect its root and that of its parent (commit1) to be found in
  // GetLiveRootIdentifiers
  std::unique_ptr<const Commit> commit2 = CreateRandomCommit(commit1->Clone());
  ObjectIdentifier commit2_root = commit2->GetRootIdentifier();
  EXPECT_THAT(factory->GetLiveRootIdentifiers(), UnorderedElementsAre(commit1_root, commit2_root));

  // Add another commit as child of commit1.
  std::unique_ptr<const Commit> commit3 = CreateRandomCommit(commit1->Clone());
  ObjectIdentifier commit3_root = commit3->GetRootIdentifier();
  EXPECT_THAT(factory->GetLiveRootIdentifiers(),
              UnorderedElementsAre(commit1_root, commit2_root, commit3_root));

  // Create a merge commit from commit2 and commit3: only the base commit (commit2) should be added.
  ObjectIdentifier base_root;
  if (commit2->GetId() < commit3->GetId()) {
    base_root = commit2->GetRootIdentifier();
  } else {
    base_root = commit3->GetRootIdentifier();
  }
  std::unique_ptr<const Commit> merge_commit =
      CreateRandomMergeCommit(commit2->Clone(), commit3->Clone());
  ObjectIdentifier merge_commit_root = merge_commit->GetRootIdentifier();
  EXPECT_THAT(factory->GetLiveRootIdentifiers(),
              UnorderedElementsAre(commit1_root, commit2_root, commit3_root, merge_commit_root));

  // Mark commit2 and commit3 and merge_commit as synced.
  bool called;
  Status status;
  storage_->MarkCommitSynced(commit2->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  storage_->MarkCommitSynced(commit3->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);

  EXPECT_THAT(factory->GetLiveRootIdentifiers(),
              UnorderedElementsAre(base_root, merge_commit_root));

  storage_->MarkCommitSynced(merge_commit->GetId(),
                             callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(factory->GetLiveRootIdentifiers(), IsEmpty());
}

// Tests that DeleteCommits updates the set of root identifiers. During this test the following
// commit graph is created:
//
// commit1 -> commit2 -> commit3
TEST_F(CommitFactoryTest, GetLiveRootIdentifiersOnDeleteCommits) {
  RunInCoroutine([this](coroutine::CoroutineHandler* handler) {
    CommitFactory* factory = storage_->GetCommitFactory();

    std::unique_ptr<const Commit> commit1 = GetFirstHead();
    ObjectIdentifier commit1_root = commit1->GetRootIdentifier();

    EXPECT_THAT(factory->GetLiveRootIdentifiers(), IsEmpty());

    // Add a commit and expect its root and that of its parent (commit1) to be found in
    // GetLiveRootIdentifiers
    std::unique_ptr<const Commit> commit2 = CreateRandomCommit(commit1->Clone());
    ObjectIdentifier commit2_root = commit2->GetRootIdentifier();
    EXPECT_THAT(factory->GetLiveRootIdentifiers(),
                UnorderedElementsAre(commit1_root, commit2_root));

    // Add another commit as child of commit2.
    std::unique_ptr<const Commit> commit3 = CreateRandomCommit(commit2->Clone());
    ObjectIdentifier commit3_root = commit3->GetRootIdentifier();
    EXPECT_THAT(factory->GetLiveRootIdentifiers(),
                UnorderedElementsAre(commit1_root, commit2_root, commit3_root));

    // Delete commit1. Nothing should change: commit1_root is a dependency for commit2.
    std::vector<std::unique_ptr<const Commit>> commits;
    commits.emplace_back(std::move(commit1));
    Status status = storage_->DeleteCommits(handler, std::move(commits));
    EXPECT_EQ(status, Status::OK);
    EXPECT_THAT(factory->GetLiveRootIdentifiers(),
                UnorderedElementsAre(commit1_root, commit2_root, commit3_root));

    // Delete commit2: commit1_root should be removed. commit2_root stays alive because it is a
    // dependency of commit3.
    commits.clear();
    commits.emplace_back(std::move(commit2));
    status = storage_->DeleteCommits(handler, std::move(commits));
    EXPECT_EQ(status, Status::OK);
    EXPECT_THAT(factory->GetLiveRootIdentifiers(),
                UnorderedElementsAre(commit2_root, commit3_root));

    // Delete commit3. Now that all commits are deleted the set should be empty.
    commits.clear();
    commits.emplace_back(std::move(commit3));
    status = storage_->DeleteCommits(handler, std::move(commits));
    EXPECT_EQ(status, Status::OK);
    EXPECT_THAT(factory->GetLiveRootIdentifiers(), IsEmpty());
  });
}

}  // namespace
}  // namespace storage
