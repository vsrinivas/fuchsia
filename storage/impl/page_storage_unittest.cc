// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/page_storage_impl.h"

#include <memory>

#include "apps/ledger/glue/crypto/hash.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/impl/commit_impl.h"
#include "apps/ledger/storage/public/constants.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace storage {
namespace {

std::string RandomId(size_t size) {
  std::string result;
  result.resize(size);
  glue::RandBytes(&result[0], size);
  return result;
}

std::string ToHex(const std::string& string) {
  std::string result;
  for (char c : string) {
    if (c >= 0 && c < 16)
      result += "0";
    result += NumberToString(static_cast<uint8_t>(c), ftl::Base::k16);
  }
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
    storage_.reset(
        new PageStorageImpl(message_loop_.task_runner(), tmp_dir_.path(), id));
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

  mtl::MessageLoop message_loop_;
  files::ScopedTempDir tmp_dir_;

 private:
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
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(),
                                              JournalType::EXPLICIT, &journal));
  EXPECT_NE(nullptr, journal);
  journal.reset();

  // Implicit journal.
  EXPECT_EQ(Status::OK, storage_->StartCommit(GetFirstHead(),
                                              JournalType::IMPLICIT, &journal));
  EXPECT_NE(nullptr, journal);
  journal.reset();

  // Journal for merge commit.
  EXPECT_EQ(Status::OK,
            storage_->StartMergeCommit(RandomId(kCommitIdSize),
                                       RandomId(kCommitIdSize), &journal));
  EXPECT_NE(nullptr, journal);
}

TEST_F(PageStorageTest, AddObjectFromLocal) {
  std::string content("Some data");

  Status status;
  ObjectId object_id;
  storage_->AddObjectFromLocal(
      mtl::WriteStringToConsumerHandle(content), content.size(),
      [this, &status, &object_id](Status returned_status,
                                  ObjectId returned_object_id) {
        status = returned_status;
        object_id = std::move(returned_object_id);
        message_loop_.QuitNow();
      });
  message_loop_.Run();
  EXPECT_EQ(Status::OK, status);
  std::string hash = glue::SHA256Hash(content.data(), content.size());
  EXPECT_EQ(hash, object_id);

  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(object_id);
  std::string file_content;
  EXPECT_TRUE(files::ReadFileToString(file_path, &file_content));
  EXPECT_EQ(content, file_content);
}

TEST_F(PageStorageTest, GetObject) {
  std::string content("Some data");
  ObjectId object_id = glue::SHA256Hash(content.data(), content.size());
  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(object_id);
  ASSERT_TRUE(files::WriteFile(file_path, content.data(), content.size()));

  Status status;
  std::unique_ptr<Object> object;
  storage_->GetObject(object_id, [this, &status, &object](
                                     Status returned_status,
                                     std::unique_ptr<Object> returned_object) {
    status = returned_status;
    object = std::move(returned_object);
    message_loop_.QuitNow();
  });
  message_loop_.Run();

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(object_id, object->GetId());
  ftl::StringView data;
  ASSERT_EQ(Status::OK, object->GetData(&data));
  EXPECT_EQ(content, convert::ToString(data));
}

TEST_F(PageStorageTest, AddObjectSynchronous) {
  std::string content("Some data");

  std::unique_ptr<Object> object;
  Status status = storage_->AddObjectSynchronous(content, &object);
  EXPECT_EQ(Status::OK, status);
  std::string hash = glue::SHA256Hash(content.data(), content.size());
  EXPECT_EQ(hash, object->GetId());

  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(hash);
  std::string file_content;
  EXPECT_TRUE(files::ReadFileToString(file_path, &file_content));
  EXPECT_EQ(content, file_content);
}

TEST_F(PageStorageTest, GetObjectSynchronous) {
  std::string content("Some data");
  ObjectId object_id = glue::SHA256Hash(content.data(), content.size());
  std::string file_path = tmp_dir_.path() + "/objects/" + ToHex(object_id);
  ASSERT_TRUE(files::WriteFile(file_path, content.data(), content.size()));

  std::unique_ptr<Object> object;
  Status status = storage_->GetObjectSynchronous(object_id, &object);

  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(object_id, object->GetId());
  ftl::StringView data;
  ASSERT_EQ(Status::OK, object->GetData(&data));
  EXPECT_EQ(content, convert::ToString(data));
}

}  // namespace
}  // namespace storage
