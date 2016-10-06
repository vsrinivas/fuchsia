// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/application_storage_impl.h"

#include <memory>

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/glue/test/run_loop.h"
#include "apps/ledger/storage/impl/commit_impl.h"
#include "apps/ledger/storage/public/constants.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace storage {
namespace {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

void CheckCommitStorageBytes(const CommitId& id, const Commit& commit) {
  std::unique_ptr<Commit> copy =
      CommitImpl::FromStorageBytes(id, commit.GetStorageBytes());
  EXPECT_EQ(commit.GetId(), copy->GetId());
  EXPECT_EQ(commit.GetTimestamp(), copy->GetTimestamp());
  EXPECT_EQ(commit.GetParentIds(), copy->GetParentIds());
  // TODO(nellyv): Check that the root node is also correctly (de)serialized.
}

class ApplicationStorageTest : public ::testing::Test {
 public:
  ApplicationStorageTest() {}

  ~ApplicationStorageTest() override {}

  // Test:
  void SetUp() override {
    storage_.reset(new ApplicationStorageImpl(message_loop_.task_runner(),
                                              tmp_dir_.path()));
    std::srand(0);
  }

 protected:
  mtl::MessageLoop message_loop_;
  std::unique_ptr<ApplicationStorageImpl> storage_;

 private:
  files::ScopedTempDir tmp_dir_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationStorageTest);
};

TEST_F(ApplicationStorageTest, CreateGetCreatePageStorage) {
  PageId pageId = "1234";
  storage_->GetPageStorage(pageId,
                           [](std::unique_ptr<PageStorage> page_storage) {
                             EXPECT_EQ(page_storage, nullptr);
                             glue::test::QuitLoop();
                           });
  glue::test::RunLoop();

  std::unique_ptr<PageStorage> pageStorage =
      storage_->CreatePageStorage(pageId);
  EXPECT_EQ(pageId, pageStorage->GetId());
  storage_->GetPageStorage(pageId,
                           [](std::unique_ptr<PageStorage> page_storage) {
                             EXPECT_NE(page_storage, nullptr);
                             glue::test::QuitLoop();
                           });
  glue::test::RunLoop();
}

TEST_F(ApplicationStorageTest, CreateDeletePageStorage) {
  PageId pageId = "1234";
  std::unique_ptr<PageStorage> pageStorage =
      storage_->CreatePageStorage(pageId);
  EXPECT_EQ(pageId, pageStorage->GetId());
  storage_->GetPageStorage(pageId,
                           [](std::unique_ptr<PageStorage> page_storage) {
                             EXPECT_NE(page_storage, nullptr);
                             glue::test::QuitLoop();
                           });
  glue::test::RunLoop();

  EXPECT_TRUE(storage_->DeletePageStorage(pageId));
  storage_->GetPageStorage(pageId,
                           [](std::unique_ptr<PageStorage> page_storage) {
                             EXPECT_EQ(nullptr, page_storage);
                             glue::test::QuitLoop();
                           });
  glue::test::RunLoop();
}

TEST_F(ApplicationStorageTest, Commit) {
  CommitId id = RandomId(kCommitIdSize);
  int64_t timestamp = 1234;
  ObjectId rootNodeId = RandomId(kObjectIdSize);

  std::vector<CommitId> parents;
  parents.push_back(RandomId(kCommitIdSize));

  // A commit with one parent.
  CommitImpl commit(id, timestamp, rootNodeId, parents);
  CheckCommitStorageBytes(id, commit);

  // A commit with two parents.
  parents.push_back(RandomId(kCommitIdSize));
  CommitImpl commit2(id, timestamp, rootNodeId, parents);
  CheckCommitStorageBytes(id, commit2);
}

}  // namespace
}  // namespace storage
