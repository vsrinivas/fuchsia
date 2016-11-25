// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/commit_impl.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/storage/test/commit_random_impl.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"

namespace storage {
namespace {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

class CommitImplTest : public ::testing::Test {
 public:
  CommitImplTest() : page_storage_(ObjectId(kObjectIdSize, 'a')) {}

  ~CommitImplTest() override {}

  void CheckCommitStorageBytes(const std::unique_ptr<Commit>& commit) {
    std::unique_ptr<Commit> copy = CommitImpl::FromStorageBytes(
        &page_storage_, commit->GetId(), commit->GetStorageBytes());
    EXPECT_EQ(commit->GetId(), copy->GetId());
    EXPECT_EQ(commit->GetTimestamp(), copy->GetTimestamp());
    EXPECT_EQ(commit->GetParentIds(), copy->GetParentIds());
    EXPECT_EQ(commit->GetRootId(), copy->GetRootId());
  }

 protected:
  fake::FakePageStorage page_storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CommitImplTest);
};

TEST_F(CommitImplTest, CommitStorageBytes) {
  ObjectId root_node_id = RandomId(kObjectIdSize);

  std::vector<std::unique_ptr<const Commit>> parents;
  parents.emplace_back(new test::CommitRandomImpl());

  // A commit with one parent.
  std::unique_ptr<Commit> commit = CommitImpl::FromContentAndParents(
      &page_storage_, root_node_id, std::move(parents));
  CheckCommitStorageBytes(commit);

  // A commit with two parents.
  parents = std::vector<std::unique_ptr<const Commit>>();
  parents.emplace_back(new test::CommitRandomImpl());
  parents.emplace_back(new test::CommitRandomImpl());
  std::unique_ptr<Commit> commit2 = CommitImpl::FromContentAndParents(
      &page_storage_, root_node_id, std::move(parents));
  CheckCommitStorageBytes(commit2);
}

}  // namespace
}  // namespace storage
