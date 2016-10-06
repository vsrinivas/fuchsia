// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/page_storage_impl.h"

#include <memory>

#include "apps/ledger/glue/crypto/rand.h"
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

class PageStorageTest : public ::testing::Test {
 public:
  PageStorageTest() {}

  ~PageStorageTest() override {}

  // Test:
  void SetUp() override {
    std::srand(0);
    PageId id = RandomId(16);
    storage_.reset(new PageStorageImpl(tmp_dir_.path(), id));
    EXPECT_EQ(Status::OK, storage_->Init());
    EXPECT_EQ(id, storage_->GetId());
  }

 protected:
  CommitId GetFirstHead() {
    std::vector<CommitId> ids;
    EXPECT_EQ(Status::OK, storage_->GetHeadCommitIds(&ids));
    EXPECT_FALSE(ids.empty());
    return ids[0];
  }

 private:
  files::ScopedTempDir tmp_dir_;

 protected:
  std::unique_ptr<PageStorageImpl> storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageStorageTest);
};

TEST_F(PageStorageTest, AddGetLocalCommits) {
  CommitId id = RandomId(kCommitIdSize);

  // Search for a commit id that doesn't exist and see the error.
  std::unique_ptr<Commit> commit;
  EXPECT_EQ(Status::NOT_FOUND, storage_->GetCommit(id, &commit));
  EXPECT_FALSE(commit);

  commit.reset(
      new CommitImpl(id, 0, RandomId(kObjectIdSize), {GetFirstHead()}));
  std::string storage_bytes = commit->GetStorageBytes();

  // Search for a commit that exist and check the content.
  EXPECT_EQ(Status::OK, storage_->AddCommitFromLocal(std::move(commit)));
  std::unique_ptr<Commit> found;
  EXPECT_EQ(Status::OK, storage_->GetCommit(id, &found));
  EXPECT_EQ(storage_bytes, found->GetStorageBytes());
}

TEST_F(PageStorageTest, AddGetSyncedCommits) {
  CommitId id = RandomId(kCommitIdSize);

  std::unique_ptr<Commit> commit(
      new CommitImpl(id, 0, RandomId(kObjectIdSize), {GetFirstHead()}));

  EXPECT_EQ(Status::OK,
            storage_->AddCommitFromSync(id, commit->GetStorageBytes()));

  std::unique_ptr<Commit> found;
  EXPECT_EQ(Status::OK, storage_->GetCommit(id, &found));
  EXPECT_EQ(commit->GetStorageBytes(), found->GetStorageBytes());

  // Check that the commit is not marked as unsynced.
  std::vector<std::unique_ptr<Commit>> commits;
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_TRUE(commits.empty());
}

TEST_F(PageStorageTest, SyncCommits) {
  std::vector<std::unique_ptr<Commit>> commits;

  // Initially there should be no unsynced commits.
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_TRUE(commits.empty());

  // After adding a commit it should marked as unsynced.
  CommitId id = RandomId(kCommitIdSize);
  std::unique_ptr<Commit> commit(
      new CommitImpl(id, 0, RandomId(kObjectIdSize), {GetFirstHead()}));
  std::string storage_bytes = commit->GetStorageBytes();

  EXPECT_EQ(Status::OK, storage_->AddCommitFromLocal(std::move(commit)));
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_EQ(1u, commits.size());
  EXPECT_EQ(storage_bytes, commits[0]->GetStorageBytes());

  // Mark it as synced.
  EXPECT_EQ(Status::OK, storage_->MarkCommitSynced(id));
  EXPECT_EQ(Status::OK, storage_->GetUnsyncedCommits(&commits));
  EXPECT_TRUE(commits.empty());
}

TEST_F(PageStorageTest, HeadCommits) {
  // Every page should have one initial head commit.
  std::vector<CommitId> heads;
  EXPECT_EQ(Status::OK, storage_->GetHeadCommitIds(&heads));
  EXPECT_EQ(1u, heads.size());

  // Adding a new commit with the previous head as its parent should replace the
  // old head.
  CommitId id = RandomId(kCommitIdSize);
  std::unique_ptr<Commit> commit(
      new CommitImpl(id, 0, RandomId(kObjectIdSize), {heads[0]}));

  EXPECT_EQ(Status::OK, storage_->AddCommitFromLocal(std::move(commit)));
  EXPECT_EQ(Status::OK, storage_->GetHeadCommitIds(&heads));
  EXPECT_EQ(1u, heads.size());
  EXPECT_EQ(id, heads[0]);
}

TEST_F(PageStorageTest, CreateJournals) {
  std::unique_ptr<Journal> journal;

  // Explicit journal.
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(), false, &journal));
  EXPECT_NE(nullptr, journal);
  journal.reset();

  // Implicit journal.
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(), true, &journal));
  EXPECT_NE(nullptr, journal);
  journal.reset();

  // Journal for merge commit.
  EXPECT_EQ(Status::OK,
            storage_->StartMergeCommit(RandomId(kCommitIdSize),
                                       RandomId(kCommitIdSize), &journal));
  EXPECT_NE(nullptr, journal);
}

}  // namespace
}  // namespace storage
