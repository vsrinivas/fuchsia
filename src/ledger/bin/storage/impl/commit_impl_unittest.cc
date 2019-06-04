// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/commit_impl.h"

#include <tuple>

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/commit_random_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/lib/fxl/macros.h"

namespace storage {
namespace {

class CommitImplTest : public StorageTest {
 public:
  CommitImplTest() : page_storage_(&environment_, "page_id") {}

  ~CommitImplTest() override {}

 protected:
  PageStorage* GetStorage() override { return &page_storage_; }

  bool CheckCommitEquals(const Commit& expected, const Commit& commit) {
    return std::forward_as_tuple(expected.GetId(), expected.GetTimestamp(),
                                 expected.GetParentIds(),
                                 expected.GetRootIdentifier()) ==
           std::forward_as_tuple(commit.GetId(), commit.GetTimestamp(),
                                 commit.GetParentIds(),
                                 commit.GetRootIdentifier());
  }

  bool CheckCommitStorageBytes(const std::unique_ptr<const Commit>& commit) {
    std::unique_ptr<const Commit> copy;
    Status status = CommitImpl::FromStorageBytes(
        commit->GetId(), commit->GetStorageBytes().ToString(), &copy);
    EXPECT_EQ(Status::OK, status);

    return CheckCommitEquals(*commit, *copy);
  }

  fake::FakePageStorage page_storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommitImplTest);
};

TEST_F(CommitImplTest, CommitStorageBytes) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random());

  std::vector<std::unique_ptr<const Commit>> parents;

  // A commit with one parent.
  parents.emplace_back(
      std::make_unique<CommitRandomImpl>(environment_.random()));
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit));

  // A commit with two parents.
  parents = std::vector<std::unique_ptr<const Commit>>();
  parents.emplace_back(
      std::make_unique<CommitRandomImpl>(environment_.random()));
  parents.emplace_back(
      std::make_unique<CommitRandomImpl>(environment_.random()));
  std::unique_ptr<const Commit> commit2 = CommitImpl::FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));
  EXPECT_TRUE(CheckCommitStorageBytes(commit2));
}

TEST_F(CommitImplTest, CloneCommit) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random());

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(
      std::make_unique<CommitRandomImpl>(environment_.random()));
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));
  std::unique_ptr<const Commit> copy;
  Status status = CommitImpl::FromStorageBytes(
      commit->GetId(), commit->GetStorageBytes().ToString(), &copy);
  ASSERT_EQ(Status::OK, status);
  std::unique_ptr<const Commit> clone = commit->Clone();
  EXPECT_TRUE(CheckCommitEquals(*copy, *clone));
}

TEST_F(CommitImplTest, MergeCommitTimestamp) {
  ObjectIdentifier root_node_identifier =
      RandomObjectIdentifier(environment_.random());

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(
      std::make_unique<CommitRandomImpl>(environment_.random()));
  parents.emplace_back(
      std::make_unique<CommitRandomImpl>(environment_.random()));
  EXPECT_NE(parents[0]->GetTimestamp(), parents[1]->GetTimestamp());
  auto max_timestamp =
      std::max(parents[0]->GetTimestamp(), parents[1]->GetTimestamp());
  std::unique_ptr<const Commit> commit = CommitImpl::FromContentAndParents(
      environment_.clock(), root_node_identifier, std::move(parents));

  EXPECT_EQ(max_timestamp, commit->GetTimestamp());
}

}  // namespace
}  // namespace storage
