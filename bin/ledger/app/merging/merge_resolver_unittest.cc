// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/merge_resolver.h"

#include <string>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/merging/last_one_wins_merge_strategy.h"
#include "apps/ledger/src/app/merging/test_utils.h"
#include "apps/ledger/src/callback/cancellable_helper.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {
class RecordingTestStrategy : public MergeStrategy {
 public:
  RecordingTestStrategy() {}
  ~RecordingTestStrategy() override {}
  void SetOnError(ftl::Closure on_error) override {
    this->on_error = std::move(on_error);
  }

  void Merge(storage::PageStorage* storage,
             PageManager* page_manager,
             std::unique_ptr<const storage::Commit> head_1,
             std::unique_ptr<const storage::Commit> head_2,
             std::unique_ptr<const storage::Commit> ancestor,
             ftl::Closure on_done) override {
    this->on_done = std::move(on_done);
    merge_calls++;
  }

  void Cancel() override { cancel_calls++; }

  ftl::Closure on_error;
  ftl::Closure on_done;
  uint32_t merge_calls = 0;
  uint32_t cancel_calls = 0;
};

class MergeResolverTest : public test::TestWithMessageLoop {
 public:
  MergeResolverTest()
      : environment_(message_loop_.task_runner(),
                     nullptr,
                     message_loop_.task_runner()) {}
  ~MergeResolverTest() override {}

 protected:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_storage_.reset(new storage::PageStorageImpl(
        environment_.main_runner(), environment_.main_runner(),
        &coroutine_service_, tmp_dir_.path(), kRootPageId.ToString()));
    storage::Status status;
    page_storage_->Init(callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, status);
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
    page_storage_->CommitJournal(
        std::move(journal),
        callback::Capture(MakeQuitTask(), &actual_status, &actual_commit));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, actual_status);
    return actual_commit->GetId();
  }

  storage::CommitId CreateMergeCommit(
      storage::CommitIdView parent_id1,
      storage::CommitIdView parent_id2,
      std::function<void(storage::Journal*)> contents) {
    std::unique_ptr<storage::Journal> journal;
    EXPECT_EQ(storage::Status::OK,
              page_storage_->StartMergeCommit(parent_id1.ToString(),
                                              parent_id2.ToString(), &journal));
    contents(journal.get());
    storage::Status actual_status;
    std::unique_ptr<const storage::Commit> actual_commit;
    page_storage_->CommitJournal(
        std::move(journal),
        callback::Capture(MakeQuitTask(), &actual_status, &actual_commit));
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
        callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());

    EXPECT_EQ(storage::Status::OK, status);
    return result;
  }

  coroutine::CoroutineServiceImpl coroutine_service_;
  std::unique_ptr<storage::PageStorageImpl> page_storage_;
  Environment environment_;

 private:
  files::ScopedTempDir tmp_dir_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MergeResolverTest);
};

TEST_F(MergeResolverTest, Empty) {
  // Set up conflict
  CreateCommit(storage::kFirstPageCommitId,
               testing::AddKeyValueToJournal("foo", "bar"));
  CreateCommit(storage::kFirstPageCommitId,
               testing::AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<testing::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(MakeQuitTask());
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
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, testing::AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_2, testing::AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 =
      CreateCommit(commit_2, testing::DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 =
      CreateCommit(commit_4, testing::AddKeyValueToJournal("key2", "val2.1"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<VerifyingMergeStrategy> strategy =
      std::make_unique<VerifyingMergeStrategy>(message_loop_.task_runner(),
                                               commit_5, commit_3, commit_2);
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<testing::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.QuitNow(); });
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
}

TEST_F(MergeResolverTest, LastOneWins) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, testing::AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_2, testing::AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 =
      CreateCommit(commit_2, testing::DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 =
      CreateCommit(commit_4, testing::AddKeyValueToJournal("key2", "val2.1"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<testing::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(MakeQuitTask());

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
  storage::Status status;
  std::unique_ptr<const storage::Commit> commit;
  page_storage_->GetCommit(
      ids[0], ::callback::Capture(MakeQuitTask(), &status, &commit));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);

  std::vector<storage::Entry> content_vector = GetCommitContents(*commit);
  // Entries are ordered by keys
  ASSERT_EQ(2u, content_vector.size());
  EXPECT_EQ("key2", content_vector[0].key);
  EXPECT_EQ(testing::MakeObjectId("val2.1"), content_vector[0].object_id);
  EXPECT_EQ("key3", content_vector[1].key);
  EXPECT_EQ(testing::MakeObjectId("val3.0"), content_vector[1].object_id);
}

TEST_F(MergeResolverTest, None) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, testing::AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_2, testing::AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 =
      CreateCommit(commit_2, testing::DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 =
      CreateCommit(commit_4, testing::AddKeyValueToJournal("key2", "val2.1"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<testing::TestBackoff>(nullptr));
  resolver.set_on_empty(MakeQuitTask());

  EXPECT_TRUE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
}

TEST_F(MergeResolverTest, UpdateMidResolution) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, testing::AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_1, testing::AddKeyValueToJournal("key3", "val3.0"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_2));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<testing::TestBackoff>(nullptr));
  resolver.set_on_empty(MakeQuitTask());
  resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  message_loop_.task_runner()->PostTask([&resolver] {
    resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  });

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
}

TEST_F(MergeResolverTest, WaitOnMergeOfMerges) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_3 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId merge_1 = CreateMergeCommit(
      commit_1, commit_3, testing::AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId merge_2 = CreateMergeCommit(
      commit_2, commit_3, testing::AddKeyValueToJournal("key3", "val3.0"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), merge_1));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), merge_2));

  int get_next_count = 0;
  MergeResolver resolver(
      [] {}, &environment_, page_storage_.get(),
      std::make_unique<testing::TestBackoff>(&get_next_count));
  resolver.set_on_empty(MakeQuitTask());
  resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
  EXPECT_GT(get_next_count, 0);
}

TEST_F(MergeResolverTest, AutomaticallyMergeIdenticalCommits) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(storage::kFirstPageCommitId,
                   testing::AddKeyValueToJournal("key1", "val1.0"));

  std::vector<storage::CommitId> ids;
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_1));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_2));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<testing::TestBackoff>(nullptr));
  resolver.set_on_empty(MakeQuitTask());
  auto merge_strategy = std::make_unique<RecordingTestStrategy>();
  auto merge_strategy_ptr = merge_strategy.get();
  resolver.SetMergeStrategy(std::move(merge_strategy));

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(storage::Status::OK, page_storage_->GetHeadCommitIds(&ids));
  EXPECT_EQ(1u, ids.size());
  EXPECT_EQ(0u, merge_strategy_ptr->merge_calls);
}

}  // namespace
}  // namespace ledger
