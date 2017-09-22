// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/commit_impl.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/impl/commit_random_impl.h"
#include "apps/ledger/src/storage/impl/storage_test_utils.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"

namespace storage {
namespace {

class CommitImplTest : public StorageTest {
 public:
  CommitImplTest() : page_storage_("page_id") {}

  ~CommitImplTest() override {}

 protected:
  PageStorage* GetStorage() override { return &page_storage_; }

  bool CheckCommitEquals(const Commit& expected, const Commit& commit) {
    return (expected.GetId() == commit.GetId()) &&
           (expected.GetTimestamp() == commit.GetTimestamp()) &&
           (expected.GetParentIds() == commit.GetParentIds()) &&
           (expected.GetRootId() == commit.GetRootId());
  }

  bool CheckCommitStorageBytes(const std::unique_ptr<Commit>& commit) {
    std::unique_ptr<Commit> copy = CommitImpl::FromStorageBytes(
        &page_storage_, commit->GetId(), commit->GetStorageBytes().ToString());

    return CheckCommitEquals(*commit, *copy);
  }

  fake::FakePageStorage page_storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommitImplTest);
};

TEST_F(CommitImplTest, CommitStorageBytes) {
  ObjectId root_node_id = RandomObjectId();

  std::vector<std::unique_ptr<const Commit>> parents;

  // A commit with one parent.
  parents.emplace_back(new test::CommitRandomImpl());
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      &page_storage_, root_node_id, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit));

  // A commit with two parents.
  parents = std::vector<std::unique_ptr<const Commit>>();
  parents.emplace_back(new test::CommitRandomImpl());
  parents.emplace_back(new test::CommitRandomImpl());
  std::unique_ptr<Commit> commit2 = CommitImpl::FromContentAndParents(
      &page_storage_, root_node_id, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit2));
}

TEST_F(CommitImplTest, CloneCommit) {
  ObjectId root_node_id = RandomObjectId();

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(new test::CommitRandomImpl());
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      &page_storage_, root_node_id, std::move(parents));
  std::unique_ptr<Commit> copy = CommitImpl::FromStorageBytes(
      &page_storage_, commit->GetId(), commit->GetStorageBytes().ToString());
  std::unique_ptr<Commit> clone = commit->Clone();
  EXPECT_TRUE(CheckCommitEquals(*copy, *clone));
}

TEST_F(CommitImplTest, MergeCommitTimestamp) {
  ObjectId root_node_id = RandomObjectId();

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(new test::CommitRandomImpl());
  parents.emplace_back(new test::CommitRandomImpl());
  EXPECT_NE(parents[0]->GetTimestamp(), parents[1]->GetTimestamp());
  auto max_timestamp =
      std::max(parents[0]->GetTimestamp(), parents[1]->GetTimestamp());
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      &page_storage_, root_node_id, std::move(parents));

  EXPECT_EQ(max_timestamp, commit->GetTimestamp());
}

}  // namespace
}  // namespace storage
