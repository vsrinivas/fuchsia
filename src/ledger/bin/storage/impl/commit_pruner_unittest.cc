// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_pruner.h"

#include <vector>

// gtest matchers are in gmock and we cannot include the specific header file
// directly as it is private to the library.
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/callback/set_when_called.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/lib/fxl/macros.h"

using testing::IsEmpty;
using testing::SizeIs;

namespace storage {
namespace {

class FakePageStorage : public PageStorageEmptyImpl {
 public:
  void AddCommit(std::unique_ptr<const Commit> commit) {
    CommitId id = commit->GetId();
    commits_[std::move(id)] = std::move(commit);
  }

  void GetCommit(CommitIdView commit_id,
                 fit::function<void(Status, std::unique_ptr<const Commit>)> callback) override {
    auto it = commits_.find(commit_id);
    if (it == commits_.end()) {
      callback(Status::INTERNAL_NOT_FOUND, nullptr);
      return;
    }
    callback(Status::OK, it->second->Clone());
  }

  void DeleteCommits(std::vector<std::unique_ptr<const Commit>> commits,
                     fit::function<void(Status)> callback) override {
    delete_commit_calls_.emplace_back(std::move(commits), std::move(callback));
  }

  std::vector<std::pair<std::vector<std::unique_ptr<const Commit>>, fit::function<void(Status)>>>
      delete_commit_calls_;

 private:
  std::map<CommitId, std::unique_ptr<const Commit>, convert::StringViewComparator> commits_;
};

class CommitPrunerTest : public ledger::TestWithEnvironment {
 public:
  CommitPrunerTest() {}

  ~CommitPrunerTest() override {}

 protected:
 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommitPrunerTest);
};

TEST_F(CommitPrunerTest, NoPruningPolicy) {
  LiveCommitTracker tracker;
  FakePageStorage storage;

  CommitPruner pruner(&environment_, &storage, &tracker, CommitPruningPolicy::NEVER);

  // Add some commits.
  std::unique_ptr<const Commit> commit_0 =
      std::make_unique<const CommitRandomImpl>(environment_.random());
  storage.AddCommit(std::move(commit_0));
  std::unique_ptr<Commit> commit_1 = std::make_unique<CommitRandomImpl>(environment_.random());
  Commit* commit_1_ptr = commit_1.get();
  tracker.RegisterCommit(commit_1_ptr);
  storage.AddCommit(std::move(commit_1));

  std::unique_ptr<Commit> commit_2 = std::make_unique<CommitRandomImpl>(environment_.random());
  Commit* commit_2_ptr = commit_2.get();
  tracker.RegisterCommit(commit_2.get());
  storage.AddCommit(std::move(commit_2));

  bool called;
  Status status;
  pruner.Prune(callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  EXPECT_THAT(storage.delete_commit_calls_, IsEmpty());

  tracker.UnregisterCommit(commit_1_ptr);
  tracker.UnregisterCommit(commit_2_ptr);
}

class FakeCommit : public CommitRandomImpl {
 public:
  FakeCommit(rng::Random* random, CommitId parent, uint64_t generation)
      : CommitRandomImpl(random), generation_(generation) {
    parents_.push_back(std::move(parent));
  }

  FakeCommit(rng::Random* random, CommitId parent_1, CommitId parent_2, uint64_t generation)
      : CommitRandomImpl(random), generation_(generation) {
    parents_.push_back(std::move(parent_1));
    parents_.push_back(std::move(parent_2));
  }

  FakeCommit(const FakeCommit& other) = default;
  FakeCommit& operator=(const FakeCommit& other) = default;

  std::vector<CommitIdView> GetParentIds() const override {
    std::vector<CommitIdView> result;
    for (const CommitId& commit_id : parents_) {
      result.emplace_back(commit_id);
    }
    return result;
  }

  uint64_t GetGeneration() const override { return generation_; }

  std::unique_ptr<const Commit> Clone() const override {
    return std::make_unique<FakeCommit>(*this);
  }

 private:
  std::vector<CommitId> parents_;
  uint64_t generation_;
};

// Verify that only commits before the latest unique common ancestor are pruned. Here, we have the
// following commit graph:
//   0
//   |
//   1
//  / \
// 2   3
//  \ /
//   4
// where commits 0, 2, 3 and 4 are live. No commit should be pruned.
TEST_F(CommitPrunerTest, PruneBeforeLucaNoPruning) {
  LiveCommitTracker tracker;
  FakePageStorage storage;

  CommitPruner pruner(&environment_, &storage, &tracker, CommitPruningPolicy::LOCAL_IMMEDIATE);

  // Add some commits. The parent of commit 0 does not exist in the database.
  std::unique_ptr<Commit> commit_0 =
      std::make_unique<FakeCommit>(environment_.random(), "random_commit_id", 10);
  CommitId commit_id_0 = commit_0->GetId();
  Commit* commit_0_ptr = commit_0.get();
  tracker.RegisterCommit(commit_0_ptr);
  storage.AddCommit(std::move(commit_0));

  std::unique_ptr<Commit> commit_1 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_0, 11);
  CommitId commit_id_1 = commit_1->GetId();
  storage.AddCommit(std::move(commit_1));

  std::unique_ptr<Commit> commit_2 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_1, 12);
  CommitId commit_id_2 = commit_2->GetId();
  Commit* commit_2_ptr = commit_2.get();
  tracker.RegisterCommit(commit_2_ptr);
  storage.AddCommit(std::move(commit_2));

  std::unique_ptr<Commit> commit_3 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_1, 12);
  CommitId commit_id_3 = commit_3->GetId();
  Commit* commit_3_ptr = commit_3.get();
  tracker.RegisterCommit(commit_3_ptr);
  storage.AddCommit(std::move(commit_3));

  std::unique_ptr<Commit> commit_4 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_2, commit_id_3, 13);
  Commit* commit_4_ptr = commit_4.get();
  tracker.RegisterCommit(commit_4_ptr);
  storage.AddCommit(std::move(commit_4));

  bool called;
  Status status;
  pruner.Prune(callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(storage.delete_commit_calls_, IsEmpty());

  tracker.UnregisterCommit(commit_0_ptr);
  tracker.UnregisterCommit(commit_2_ptr);
  tracker.UnregisterCommit(commit_3_ptr);
  tracker.UnregisterCommit(commit_4_ptr);
}

// Verify that only commits before the latest unique common ancestor are pruned. Here, we have the
// following commit graph:
//   0
//   |
//   1
//  / \
// 2   3
//  \ /
//   4
// where commits 2, 3 and 4 are live. Only commit 0 should be pruned.
TEST_F(CommitPrunerTest, PruneBeforeLuca1) {
  LiveCommitTracker tracker;
  FakePageStorage storage;

  CommitPruner pruner(&environment_, &storage, &tracker, CommitPruningPolicy::LOCAL_IMMEDIATE);

  // Add some commits. The parent of commit 0 does not exist in the database.
  std::unique_ptr<Commit> commit_0 =
      std::make_unique<FakeCommit>(environment_.random(), "random_commit_id", 10);
  CommitId commit_id_0 = commit_0->GetId();
  storage.AddCommit(std::move(commit_0));

  std::unique_ptr<Commit> commit_1 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_0, 11);
  CommitId commit_id_1 = commit_1->GetId();
  storage.AddCommit(std::move(commit_1));

  std::unique_ptr<Commit> commit_2 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_1, 12);
  CommitId commit_id_2 = commit_2->GetId();
  Commit* commit_2_ptr = commit_2.get();
  tracker.RegisterCommit(commit_2_ptr);
  storage.AddCommit(std::move(commit_2));

  std::unique_ptr<Commit> commit_3 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_1, 12);
  CommitId commit_id_3 = commit_3->GetId();
  Commit* commit_3_ptr = commit_3.get();
  tracker.RegisterCommit(commit_3_ptr);
  storage.AddCommit(std::move(commit_3));

  std::unique_ptr<Commit> commit_4 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_2, commit_id_3, 13);
  Commit* commit_4_ptr = commit_4.get();
  tracker.RegisterCommit(commit_4_ptr);
  storage.AddCommit(std::move(commit_4));

  bool called;
  Status status = Status::ILLEGAL_STATE;
  pruner.Prune(callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();

  // Callback is not executed yet: pruner is waiting for the answer from storage.
  EXPECT_FALSE(called);

  EXPECT_THAT(storage.delete_commit_calls_, SizeIs(1));
  EXPECT_THAT(storage.delete_commit_calls_[0].first, SizeIs(1));
  EXPECT_EQ(commit_id_0, storage.delete_commit_calls_[0].first[0]->GetId());

  storage.delete_commit_calls_[0].second(Status::OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  tracker.UnregisterCommit(commit_2_ptr);
  tracker.UnregisterCommit(commit_3_ptr);
  tracker.UnregisterCommit(commit_4_ptr);
}

// Verify that only commits before the latest unique common ancestor are pruned. Here, we have the
// following commit graph:
//   0
//   |
//   1
//  / \
// 2   3
//  \ /
//   4
// where commit 4 is live. Commits 0, 1, 2, and 3 should be pruned.
TEST_F(CommitPrunerTest, PruneBeforeLuca2) {
  LiveCommitTracker tracker;
  FakePageStorage storage;

  CommitPruner pruner(&environment_, &storage, &tracker, CommitPruningPolicy::LOCAL_IMMEDIATE);

  // Add some commits. The parent of commit 0 does not exist in the database.
  std::unique_ptr<Commit> commit_0 =
      std::make_unique<FakeCommit>(environment_.random(), "random_commit_id", 10);
  CommitId commit_id_0 = commit_0->GetId();
  storage.AddCommit(std::move(commit_0));

  std::unique_ptr<Commit> commit_1 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_0, 11);
  CommitId commit_id_1 = commit_1->GetId();
  storage.AddCommit(std::move(commit_1));

  std::unique_ptr<Commit> commit_2 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_1, 12);
  CommitId commit_id_2 = commit_2->GetId();
  storage.AddCommit(std::move(commit_2));

  std::unique_ptr<Commit> commit_3 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_1, 12);
  CommitId commit_id_3 = commit_3->GetId();
  storage.AddCommit(std::move(commit_3));

  std::unique_ptr<Commit> commit_4 =
      std::make_unique<FakeCommit>(environment_.random(), commit_id_2, commit_id_3, 13);
  Commit* commit_4_ptr = commit_4.get();
  tracker.RegisterCommit(commit_4_ptr);
  storage.AddCommit(std::move(commit_4));

  bool called;
  Status status;
  pruner.Prune(callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();

  // Callback is not executed yet: pruner is waiting for the answer from storage.
  EXPECT_FALSE(called);

  EXPECT_THAT(storage.delete_commit_calls_, SizeIs(1));
  std::set<CommitId> golden_commit_ids{commit_id_0, commit_id_1, commit_id_2, commit_id_3};
  std::set<CommitId> actual_commit_ids;
  for (const std::unique_ptr<const Commit>& commit : storage.delete_commit_calls_[0].first) {
    actual_commit_ids.insert(commit->GetId());
  }
  EXPECT_EQ(golden_commit_ids, actual_commit_ids);

  storage.delete_commit_calls_[0].second(Status::OK);
  RunLoopUntilIdle();

  ASSERT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  tracker.UnregisterCommit(commit_4_ptr);
}

}  // namespace
}  // namespace storage
