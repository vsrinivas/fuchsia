// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/common_ancestor.h"

#include <lib/fit/function.h>

#include <algorithm>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/merging/test_utils.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

using testing::ElementsAre;
using testing::IsEmpty;
using testing::UnorderedElementsAre;

namespace ledger {
namespace {
class CommonAncestorTest : public TestWithPageStorage {
 public:
  CommonAncestorTest() = default;
  CommonAncestorTest(const CommonAncestorTest&) = delete;
  CommonAncestorTest& operator=(const CommonAncestorTest&) = delete;
  ~CommonAncestorTest() override = default;

 protected:
  storage::PageStorage* page_storage() override { return storage_.get(); }

  void SetUp() override {
    TestWithPageStorage::SetUp();
    ASSERT_TRUE(CreatePageStorage(&storage_));
  }

  std::unique_ptr<const storage::Commit> CreateCommit(
      storage::CommitIdView parent_id, fit::function<void(storage::Journal*)> contents) {
    Status status;
    bool called;
    std::unique_ptr<const storage::Commit> base;
    storage_->GetCommit(parent_id,
                        callback::Capture(callback::SetWhenCalled(&called), &status, &base));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    std::unique_ptr<storage::Journal> journal = storage_->StartCommit(std::move(base));

    contents(journal.get());
    std::unique_ptr<const storage::Commit> commit;
    storage_->CommitJournal(std::move(journal),
                            callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    return commit;
  }

  std::unique_ptr<const storage::Commit> CreateMergeCommit(
      std::unique_ptr<const storage::Commit> base_left,
      std::unique_ptr<const storage::Commit> base_right,
      fit::function<void(storage::Journal*)> contents) {
    std::unique_ptr<storage::Journal> journal =
        storage_->StartMergeCommit(std::move(base_left), std::move(base_right));

    contents(journal.get());
    Status actual_status;
    bool called;
    std::unique_ptr<const storage::Commit> actual_commit;
    storage_->CommitJournal(std::move(journal), callback::Capture(callback::SetWhenCalled(&called),
                                                                  &actual_status, &actual_commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(actual_status, Status::OK);
    return actual_commit;
  }

  std::unique_ptr<const storage::Commit> GetRoot() {
    bool called;
    Status status;
    std::unique_ptr<const storage::Commit> root;
    storage_->GetCommit(storage::kFirstPageCommitId,
                        callback::Capture(callback::SetWhenCalled(&called), &status, &root));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    return root;
  }

  std::vector<storage::CommitId> GetCommitIds(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits) {
    std::vector<storage::CommitId> ids;
    ids.reserve(commits.size());
    for (auto& commit : commits) {
      ids.push_back(commit->GetId());
    }
    return ids;
  }

  std::unique_ptr<storage::PageStorage> storage_;
};

TEST_F(CommonAncestorTest, TwoChildrenOfRoot) {
  std::unique_ptr<const storage::Commit> commit_1 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));
  std::unique_ptr<const storage::Commit> commit_2 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "b"));

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(commit_1), std::move(commit_2),
                                 &comparison, &result);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::UNORDERED);
    EXPECT_THAT(GetCommitIds(result), ElementsAre(convert::ToString(storage::kFirstPageCommitId)));
  });
}

TEST_F(CommonAncestorTest, RootAndChild) {
  std::unique_ptr<const storage::Commit> root = GetRoot();

  std::unique_ptr<const storage::Commit> child =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(root), std::move(child),
                                 &comparison, &result);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::LEFT_SUBSET_OF_RIGHT);
    EXPECT_THAT(result, IsEmpty());
  });
}

TEST_F(CommonAncestorTest, ChildAndRoot) {
  std::unique_ptr<const storage::Commit> root = GetRoot();

  std::unique_ptr<const storage::Commit> child =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(child), std::move(root),
                                 &comparison, &result);

    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::RIGHT_SUBSET_OF_LEFT);
    EXPECT_THAT(result, IsEmpty());
  });
}

// In this test the commits have the following structure:
//            (root)
//              /  \
//            (A)  (B)
//           /  \  /   \
//         (1) (merge) (2)
TEST_F(CommonAncestorTest, MergeCommitAndSomeOthers) {
  std::unique_ptr<const storage::Commit> commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));
  std::unique_ptr<const storage::Commit> commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "b"));

  std::unique_ptr<const storage::Commit> commit_merge =
      CreateMergeCommit(commit_a->Clone(), commit_b->Clone(), AddKeyValueToJournal("key", "c"));

  std::unique_ptr<const storage::Commit> commit_1 =
      CreateCommit(commit_a->GetId(), AddKeyValueToJournal("key", "1"));
  std::unique_ptr<const storage::Commit> commit_2 =
      CreateCommit(commit_b->GetId(), AddKeyValueToJournal("key", "2"));

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // LCA of (1) and (merge) needs to be (A).
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(commit_1),
                                 std::move(commit_merge), &comparison, &result);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::UNORDERED);
    EXPECT_THAT(GetCommitIds(result), ElementsAre(commit_a->GetId()));

    // LCA of (2) and (A) is (root).
    result.clear();
    status = FindCommonAncestors(handler, storage_.get(), std::move(commit_2), std::move(commit_a),
                                 &comparison, &result);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::UNORDERED);
    EXPECT_THAT(GetCommitIds(result), ElementsAre(convert::ToString(storage::kFirstPageCommitId)));
  });
}

// Regression test for LE-187.
TEST_F(CommonAncestorTest, LongChain) {
  const int length = 180;

  std::unique_ptr<const storage::Commit> commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));
  std::unique_ptr<const storage::Commit> commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "b"));

  std::unique_ptr<const storage::Commit> last_commit = std::move(commit_a);
  for (int i = 0; i < length; i++) {
    last_commit =
        CreateCommit(last_commit->GetId(), AddKeyValueToJournal(std::to_string(i), "val"));
  }

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // Ancestor of (last commit) and (b) needs to be (root).
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(last_commit),
                                 std::move(commit_b), &comparison, &result);
    // This test lasts ~2.5s on x86+qemu+kvm.
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::UNORDERED);
    EXPECT_THAT(GetCommitIds(result), ElementsAre(convert::ToString(storage::kFirstPageCommitId)));
  });
}

// Test detection of equivalent commits.
// In this test the commits have the following structure:
//      (root)
//      /   \
//     (A) (B)
//      |\ /|
//      | X |
//      |/ \|
//     (M) (N)
// Requesting the common ancestors of (M) and (N) should return an empty vector
// and EQUIVALENT.
TEST_F(CommonAncestorTest, EquivalentCommits) {
  std::unique_ptr<const storage::Commit> commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));
  std::unique_ptr<const storage::Commit> commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "b"));
  std::unique_ptr<const storage::Commit> commit_m =
      CreateMergeCommit(commit_a->Clone(), commit_b->Clone(), AddKeyValueToJournal("key", "m"));
  std::unique_ptr<const storage::Commit> commit_n =
      CreateMergeCommit(std::move(commit_a), std::move(commit_b), AddKeyValueToJournal("key", "n"));

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(commit_m), std::move(commit_n),
                                 &comparison, &result);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::EQUIVALENT);
    EXPECT_THAT(GetCommitIds(result), IsEmpty());
  });
}

// Multiple common ancestors from the same generation
// In this test, the commits have the following structure:
//       (root)
//       /    \
//     (A)    (B)
//      | \  / |
//     (C) \/ (D)
//      |  /\  |
//      | /  \ |
//     (E)    (F)
// Then the common ancestors of (E) and (F) are (A) and (B).
TEST_F(CommonAncestorTest, TwoBasesSameGeneration) {
  std::unique_ptr<const storage::Commit> commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));
  std::unique_ptr<const storage::Commit> commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "b"));
  std::unique_ptr<const storage::Commit> commit_c =
      CreateCommit(commit_a->GetId(), AddKeyValueToJournal("key", "c"));
  std::unique_ptr<const storage::Commit> commit_d =
      CreateCommit(commit_b->GetId(), AddKeyValueToJournal("key", "d"));
  std::unique_ptr<const storage::Commit> commit_e =
      CreateMergeCommit(commit_c->Clone(), commit_b->Clone(), AddKeyValueToJournal("key", "e"));
  std::unique_ptr<const storage::Commit> commit_f =
      CreateMergeCommit(commit_a->Clone(), std::move(commit_d), AddKeyValueToJournal("key", "f"));

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // The LCAs of (E) and (F) are (A) and (B)
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(commit_e), std::move(commit_f),
                                 &comparison, &result);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::UNORDERED);
    EXPECT_THAT(GetCommitIds(result), UnorderedElementsAre(commit_a->GetId(), commit_b->GetId()));
  });
}

// Merges with multiple common ancestors from different generations
// In this test, the commits have the following structure:
//       (root)
//       /    \
//      |     (X)
//      |      |
//     (A)    (B)
//      | \  / |
//     (C) \/ (D)
//      |  /\  |
//      | /  \ |
//     (E)    (F)
// The LCAs of (E) and (F) are (A) and (B).
TEST_F(CommonAncestorTest, TwoBasesDifferentGenerations) {
  std::unique_ptr<const storage::Commit> commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "a"));
  std::unique_ptr<const storage::Commit> commit_x =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key", "x"));
  std::unique_ptr<const storage::Commit> commit_b =
      CreateCommit(commit_x->GetId(), AddKeyValueToJournal("key", "b"));
  std::unique_ptr<const storage::Commit> commit_c =
      CreateCommit(commit_a->GetId(), AddKeyValueToJournal("key", "c"));
  std::unique_ptr<const storage::Commit> commit_d =
      CreateCommit(commit_b->GetId(), AddKeyValueToJournal("key", "d"));
  std::unique_ptr<const storage::Commit> commit_e =
      CreateMergeCommit(std::move(commit_c), commit_b->Clone(), AddKeyValueToJournal("key", "e"));
  std::unique_ptr<const storage::Commit> commit_f =
      CreateMergeCommit(commit_a->Clone(), std::move(commit_d), AddKeyValueToJournal("key", "f"));

  RunInCoroutine([&](coroutine::CoroutineHandler* handler) {
    // The LCAs of (E) and (F) are (A) and (B)
    Status status;
    CommitComparison comparison;
    std::vector<std::unique_ptr<const storage::Commit>> result;
    status = FindCommonAncestors(handler, storage_.get(), std::move(commit_e), std::move(commit_f),
                                 &comparison, &result);
    EXPECT_EQ(status, Status::OK);
    EXPECT_EQ(comparison, CommitComparison::UNORDERED);
    EXPECT_THAT(GetCommitIds(result), UnorderedElementsAre(commit_a->GetId(), commit_b->GetId()));
  });
}

}  // namespace
}  // namespace ledger
