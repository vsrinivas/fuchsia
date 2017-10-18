// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/merge_resolver.h"

#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/merging/last_one_wins_merge_strategy.h"
#include "peridot/bin/ledger/app/merging/test_utils.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/callback/cancellable_helper.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/glue/crypto/hash.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace ledger {
namespace {
class RecordingTestStrategy : public MergeStrategy {
 public:
  RecordingTestStrategy() {}
  ~RecordingTestStrategy() override {}
  void SetOnError(fxl::Closure on_error) override {
    this->on_error = std::move(on_error);
  }

  void SetOnMerge(fxl::Closure on_merge) { on_merge_ = on_merge; }

  void Merge(storage::PageStorage* /*storage*/,
             PageManager* /*page_manager*/,
             std::unique_ptr<const storage::Commit> /*head_1*/,
             std::unique_ptr<const storage::Commit> /*head_2*/,
             std::unique_ptr<const storage::Commit> /*ancestor*/,
             std::function<void(Status)> callback) override {
    this->callback = std::move(callback);
    merge_calls++;
    if (on_merge_) {
      on_merge_();
    }
  }

  void Cancel() override { cancel_calls++; }

  fxl::Closure on_error;

  std::function<void(Status)> callback;
  uint32_t merge_calls = 0;
  uint32_t cancel_calls = 0;

 private:
  fxl::Closure on_merge_;
};

class MergeResolverTest : public test::TestWithPageStorage {
 public:
  MergeResolverTest()
      : environment_(message_loop_.task_runner(),
                     nullptr,
                     message_loop_.task_runner()) {}
  ~MergeResolverTest() override {}

 protected:
  storage::PageStorage* page_storage() override { return page_storage_.get(); }

  void SetUp() override {
    ::testing::Test::SetUp();
    ASSERT_TRUE(CreatePageStorage(&page_storage_));
  }

  storage::CommitId CreateCommit(
      storage::CommitIdView parent_id,
      std::function<void(storage::Journal*)> contents) {
    return CreateCommit(page_storage_.get(), parent_id, std::move(contents));
  }

  storage::CommitId CreateCommit(
      storage::PageStorage* storage,
      storage::CommitIdView parent_id,
      std::function<void(storage::Journal*)> contents) {
    storage::Status status;
    std::unique_ptr<storage::Journal> journal;
    storage->StartCommit(parent_id.ToString(), storage::JournalType::IMPLICIT,
                         callback::Capture(MakeQuitTask(), &status, &journal));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, status);

    contents(journal.get());
    std::unique_ptr<const storage::Commit> commit;
    storage->CommitJournal(std::move(journal),
                           callback::Capture(MakeQuitTask(), &status, &commit));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, status);
    return commit->GetId();
  }

  storage::CommitId CreateMergeCommit(
      storage::CommitIdView parent_id1,
      storage::CommitIdView parent_id2,
      std::function<void(storage::Journal*)> contents) {
    return CreateMergeCommit(page_storage_.get(), parent_id1, parent_id2,
                             std::move(contents));
  }

  storage::CommitId CreateMergeCommit(
      storage::PageStorage* storage,
      storage::CommitIdView parent_id1,
      storage::CommitIdView parent_id2,
      std::function<void(storage::Journal*)> contents) {
    storage::Status status;
    std::unique_ptr<storage::Journal> journal;
    storage->StartMergeCommit(
        parent_id1.ToString(), parent_id2.ToString(),
        callback::Capture(MakeQuitTask(), &status, &journal));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(storage::Status::OK, status);
    contents(journal.get());
    storage::Status actual_status;
    std::unique_ptr<const storage::Commit> actual_commit;
    storage->CommitJournal(
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

  std::unique_ptr<storage::PageStorage> page_storage_;
  Environment environment_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(MergeResolverTest);
};

TEST_F(MergeResolverTest, Empty) {
  // Set up conflict
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "bar"));
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(MakeQuitTask());

  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(RunLoopUntil([&] { return resolver.IsEmpty(); }));

  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());
}

class VerifyingMergeStrategy : public MergeStrategy {
 public:
  VerifyingMergeStrategy(fxl::RefPtr<fxl::TaskRunner> task_runner,
                         storage::CommitId head1,
                         storage::CommitId head2,
                         storage::CommitId ancestor)
      : task_runner_(std::move(task_runner)),
        head1_(std::move(head1)),
        head2_(std::move(head2)),
        ancestor_(std::move(ancestor)) {}
  ~VerifyingMergeStrategy() override {}

  void SetOnError(std::function<void()> /*on_error*/) override {}

  void Merge(storage::PageStorage* /*storage*/,
             PageManager* /*page_manager*/,
             std::unique_ptr<const storage::Commit> head_1,
             std::unique_ptr<const storage::Commit> head_2,
             std::unique_ptr<const storage::Commit> ancestor,
             std::function<void(Status)> callback) override {
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
    task_runner_->PostTask(
        [callback = std::move(callback)]() { callback(Status::OK); });
  }

  void Cancel() override{};

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
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

  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<VerifyingMergeStrategy> strategy =
      std::make_unique<VerifyingMergeStrategy>(message_loop_.task_runner(),
                                               commit_5, commit_3, commit_2);
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty([this] { message_loop_.QuitNow(); });

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(RunLoopUntil([&] { return resolver.IsEmpty(); }));
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

  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(MakeQuitTaskOnce());

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(resolver.IsEmpty());

  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());

  std::unique_ptr<const storage::Commit> commit;
  page_storage_->GetCommit(
      ids[0], ::callback::Capture(MakeQuitTask(), &status, &commit));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  ASSERT_TRUE(commit);

  std::vector<storage::Entry> content_vector = GetCommitContents(*commit);
  // Entries are ordered by keys
  ASSERT_EQ(2u, content_vector.size());
  EXPECT_EQ("key2", content_vector[0].key);
  std::string value;
  EXPECT_TRUE(GetValue(content_vector[0].object_digest, &value));
  EXPECT_EQ("val2.1", value);
  EXPECT_EQ("key3", content_vector[1].key);
  EXPECT_TRUE(GetValue(content_vector[1].object_digest, &value));
  EXPECT_EQ("val3.0", value);
}

TEST_F(MergeResolverTest, None) {
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

  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.set_on_empty(MakeQuitTask());

  EXPECT_TRUE(RunLoopUntil([&] { return resolver.IsEmpty(); }));
  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
}

TEST_F(MergeResolverTest, UpdateMidResolution) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 =
      CreateCommit(commit_1, AddKeyValueToJournal("key3", "val3.0"));

  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_2));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.set_on_empty(MakeQuitTask());
  resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  message_loop_.task_runner()->PostTask([&resolver] {
    resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  });

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_TRUE(RunLoopUntil([&] { return resolver.IsEmpty(); }));
  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());
}

// Merge of merges backoff is only triggered when commits are coming from sync.
// To test this, we need to create conflicts and make it as if they are not
// created locally. This is done by preventing commit notifications for new
// commits, then issuing manually a commit notification "from sync". As this
// implies using a fake PageStorage, we don't test the resolution itself, only
// that backoff is triggered correctly.
TEST_F(MergeResolverTest, WaitOnMergeOfMerges) {
  storage::fake::FakePageStorage page_storage(&message_loop_, "page_id");

  int get_next_count = 0;
  MergeResolver resolver([] {}, &environment_, &page_storage,
                         std::make_unique<test::TestBackoff>(&get_next_count));
  resolver.set_on_empty(MakeQuitTask());
  auto strategy = std::make_unique<RecordingTestStrategy>();
  strategy->SetOnMerge(MakeQuitTask());
  resolver.SetMergeStrategy(std::move(strategy));

  // A check for conflict is posted on the run loop at the creation of the
  // resolver, and when the resolver changes strategy. Hence we need to wait
  // twice for the run loop to quit before we know it is empty.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  page_storage.SetDropCommitNotifications(true);

  // Set up conflict
  storage::CommitId commit_0 = CreateCommit(
      &page_storage, storage::kFirstPageCommitId, [](storage::Journal*) {});

  storage::CommitId commit_1 = CreateCommit(
      &page_storage, commit_0, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 = CreateCommit(
      &page_storage, commit_0, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_3 = CreateCommit(
      &page_storage, commit_0, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId merge_1 =
      CreateMergeCommit(&page_storage, commit_1, commit_3,
                        AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId merge_2 =
      CreateMergeCommit(&page_storage, commit_2, commit_3,
                        AddKeyValueToJournal("key3", "val3.0"));

  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage.GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), merge_1));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), merge_2));

  page_storage.SetDropCommitNotifications(false);

  storage::CommitWatcher* watcher = &resolver;
  watcher->OnNewCommits({}, storage::ChangeSource::SYNC);

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_GT(get_next_count, 0);
}

TEST_F(MergeResolverTest, AutomaticallyMergeIdenticalCommits) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_1));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_2));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.set_on_empty(MakeQuitTask());
  auto merge_strategy = std::make_unique<RecordingTestStrategy>();
  auto merge_strategy_ptr = merge_strategy.get();
  resolver.SetMergeStrategy(std::move(merge_strategy));

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(RunLoopUntil([&] { return resolver.IsEmpty(); }));
  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(MakeQuitTask(), &status, &ids));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());
  EXPECT_EQ(0u, merge_strategy_ptr->merge_calls);
}

}  // namespace
}  // namespace ledger
