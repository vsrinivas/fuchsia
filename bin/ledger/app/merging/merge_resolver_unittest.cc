// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/merge_resolver.h"

#include <string>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/merging/last_one_wins_merge_strategy.h"
#include "apps/ledger/src/callback/cancellable_helper.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/test/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {
std::function<void(storage::Journal*)> AddKeyValueToJournal(
    const std::string& key,
    const storage::ObjectId& object_id) {
  return [key, object_id](storage::Journal* journal) {
    EXPECT_EQ(storage::Status::OK,
              journal->Put(key, object_id, storage::KeyPriority::EAGER));
  };
}

std::function<void(storage::Journal*)> DeleteKeyFromJournal(
    const std::string& key) {
  return [key](storage::Journal* journal) {
    EXPECT_EQ(storage::Status::OK, journal->Delete(key));
  };
}

class MergeResolverTest : public test::TestWithMessageLoop {
 public:
  MergeResolverTest() {}
  ~MergeResolverTest() override {}

 protected:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_storage_.reset(new storage::PageStorageImpl(
        message_loop_.task_runner(), message_loop_.task_runner(),
        tmp_dir_.path(), kRootPageId.ToString()));
    EXPECT_EQ(storage::Status::OK, page_storage_->Init());
  }

  storage::CommitId CreateCommit(
      storage::CommitIdView parent_id,
      std::function<void(storage::Journal*)> contents) {
    std::unique_ptr<storage::Journal> journal;
    EXPECT_EQ(
        storage::Status::OK,
        page_storage_->StartCommit(parent_id.ToString(),
                                   storage::JournalType::IMPLICIT, &journal));
    contents(journal.get());
    storage::Status actual_status;
    std::unique_ptr<const storage::Commit> actual_commit;
    journal->Commit(test::Capture([this] { message_loop_.PostQuitTask(); },
                                  &actual_status, &actual_commit));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, actual_status);
    return actual_commit->GetId();
  }

  std::vector<storage::Entry> GetCommitContents(const storage::Commit& commit) {
    storage::Status status;
    std::vector<storage::Entry> result;
    auto on_next = [&result](storage::Entry e) {
      result.push_back(e);
      return true;
    };
    page_storage_->GetCommitContents(
        commit, "", std::move(on_next),
        test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
    EXPECT_FALSE(RunLoopWithTimeout());

    EXPECT_EQ(storage::Status::OK, status);
    return result;
  }

  std::unique_ptr<storage::PageStorageImpl> page_storage_;

 private:
  files::ScopedTempDir tmp_dir_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MergeResolverTest);
};

TEST_F(MergeResolverTest, Empty) {
  // Set up conflict
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "bar"));
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, page_storage_.get());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.PostQuitTask(); });
  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
}

class VerifyingMergeStrategy : public MergeStrategy {
 public:
  VerifyingMergeStrategy(ftl::RefPtr<ftl::TaskRunner> task_runner,
                         storage::CommitId head1,
                         storage::CommitId head2,
                         storage::CommitId ancestor)
      : task_runner_(task_runner),
        head1_(head1),
        head2_(head2),
        ancestor_(ancestor) {}
  ~VerifyingMergeStrategy() override {}

  void SetOnError(std::function<void()> on_error) override {}

  void Merge(storage::PageStorage* storage,
             PageManager* page_manager,
             std::unique_ptr<const storage::Commit> head_1,
             std::unique_ptr<const storage::Commit> head_2,
             std::unique_ptr<const storage::Commit> ancestor,
             ftl::Closure on_done) override {
    EXPECT_EQ(ancestor_, ancestor->GetId());
    storage::CommitId actual_head1_id = head_1->GetId();
    if (actual_head1_id != head1_ && actual_head1_id != head2_) {
      // Fail
      EXPECT_EQ(head1_, actual_head1_id);
    }
    storage::CommitId actual_head2_id = head_2->GetId();
    if (actual_head2_id != head1_ && actual_head2_id != head2_) {
      // Fail
      EXPECT_EQ(head2_, actual_head2_id);
    }
    task_runner_->PostTask(std::move(on_done));
  }

  void Cancel() override{};

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  const storage::CommitId head1_;
  const storage::CommitId head2_;
  const storage::CommitId ancestor_;
};

TEST_F(MergeResolverTest, CommonAncestor) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 =
      CreateCommit(commit_2, DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 =
      CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<VerifyingMergeStrategy> strategy =
      std::make_unique<VerifyingMergeStrategy>(message_loop_.task_runner(),
                                               commit_5, commit_3, commit_2);
  MergeResolver resolver([] {}, page_storage_.get());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.QuitNow(); });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
}

TEST_F(MergeResolverTest, LastOneWins) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 =
      CreateCommit(commit_2, DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 =
      CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, page_storage_.get());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.PostQuitTask(); });

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
  storage::Status status;
  std::unique_ptr<const storage::Commit> commit;
  page_storage_->GetCommit(
      ids[0], ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                              &commit));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);

  std::vector<storage::Entry> content_vector = GetCommitContents(*commit);
  // Entries are ordered by keys
  ASSERT_EQ(2u, content_vector.size());
  EXPECT_EQ("key2", content_vector[0].key);
  EXPECT_EQ("val2.1", content_vector[0].object_id);
  EXPECT_EQ("key3", content_vector[1].key);
  EXPECT_EQ("val3.0", content_vector[1].object_id);
}

}  // namespace
}  // namespace ledger
