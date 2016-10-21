// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/commit_impl.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/fake/fake_page_storage.h"
#include "apps/ledger/storage/impl/store/object_store.h"
#include "apps/ledger/storage/public/constants.h"
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
  CommitImplTest()
      : page_storage_(ObjectId(kObjectIdSize, 'a')),
        object_store_(&page_storage_) {}

  ~CommitImplTest() override {}

  void CheckCommitStorageBytes(const CommitId& id, const Commit& commit) {
    std::unique_ptr<Commit> copy = CommitImpl::FromStorageBytes(
        &object_store_, id, commit.GetStorageBytes());
    EXPECT_EQ(commit.GetId(), copy->GetId());
    EXPECT_EQ(commit.GetTimestamp(), copy->GetTimestamp());
    EXPECT_EQ(commit.GetParentIds(), copy->GetParentIds());
    // TODO(nellyv): Check that the root node is also correctly (de)serialized.
  }

 protected:
  fake::FakePageStorage page_storage_;
  ObjectStore object_store_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CommitImplTest);
};

TEST_F(CommitImplTest, CommitStorageBytes) {
  CommitId id = RandomId(kCommitIdSize);
  int64_t timestamp = 1234;
  ObjectId rootNodeId = RandomId(kObjectIdSize);

  std::vector<CommitId> parents;
  parents.push_back(RandomId(kCommitIdSize));

  // A commit with one parent.
  CommitImpl commit(&object_store_, id, timestamp, rootNodeId, parents);
  CheckCommitStorageBytes(id, commit);

  // A commit with two parents.
  parents.push_back(RandomId(kCommitIdSize));
  CommitImpl commit2(&object_store_, id, timestamp, rootNodeId, parents);
  CheckCommitStorageBytes(id, commit2);
}

}  // namespace
}  // namespace storage
