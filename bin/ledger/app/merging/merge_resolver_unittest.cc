// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/merge_resolver.h"

#include <string>
#include <utility>

#include <lib/async/cpp/task.h>

#include "gtest/gtest.h"
#include "lib/callback/cancellable_helper.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/merging/last_one_wins_merge_strategy.h"
#include "peridot/bin/ledger/app/merging/test_utils.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

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

  void Merge(storage::PageStorage* /*storage*/, PageManager* /*page_manager*/,
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
      : environment_(EnvironmentBuilder().SetAsync(dispatcher()).Build()) {}
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
      storage::PageStorage* storage, storage::CommitIdView parent_id,
      std::function<void(storage::Journal*)> contents) {
    bool called;
    storage::Status status;
    std::unique_ptr<storage::Journal> journal;
    storage->StartCommit(
        parent_id.ToString(), storage::JournalType::IMPLICIT,
        callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);

    contents(journal.get());
    std::unique_ptr<const storage::Commit> commit;
    storage->CommitJournal(
        std::move(journal),
        callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);
    return commit->GetId();
  }

  storage::CommitId CreateMergeCommit(
      storage::CommitIdView parent_id1, storage::CommitIdView parent_id2,
      std::function<void(storage::Journal*)> contents) {
    return CreateMergeCommit(page_storage_.get(), parent_id1, parent_id2,
                             std::move(contents));
  }

  storage::CommitId CreateMergeCommit(
      storage::PageStorage* storage, storage::CommitIdView parent_id1,
      storage::CommitIdView parent_id2,
      std::function<void(storage::Journal*)> contents) {
    bool called;
    storage::Status status;
    std::unique_ptr<storage::Journal> journal;
    storage->StartMergeCommit(
        parent_id1.ToString(), parent_id2.ToString(),
        callback::Capture(callback::SetWhenCalled(&called), &status, &journal));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(storage::Status::OK, status);
    contents(journal.get());
    storage::Status actual_status;
    std::unique_ptr<const storage::Commit> actual_commit;
    storage->CommitJournal(std::move(journal),
                           callback::Capture(callback::SetWhenCalled(&called),
                                             &actual_status, &actual_commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
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
    bool called;
    page_storage_->GetCommitContents(
        commit, "", std::move(on_next),
        callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);

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
  resolver.set_on_empty([this] { QuitLoop(); });

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());

  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsEmpty());

  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());
}

class VerifyingMergeStrategy : public MergeStrategy {
 public:
  VerifyingMergeStrategy(async_t* async, storage::CommitId head1,
                         storage::CommitId head2, storage::CommitId ancestor)
      : async_(async),
        head1_(std::move(head1)),
        head2_(std::move(head2)),
        ancestor_(std::move(ancestor)) {}
  ~VerifyingMergeStrategy() override {}

  void SetOnError(std::function<void()> /*on_error*/) override {}

  void Merge(storage::PageStorage* /*storage*/, PageManager* /*page_manager*/,
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
    async::PostTask(
        async_, [callback = std::move(callback)]() { callback(Status::OK); });
  }

  void Cancel() override{};

 private:
  async_t* const async_;
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

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<VerifyingMergeStrategy> strategy =
      std::make_unique<VerifyingMergeStrategy>(dispatcher(), commit_5, commit_3,
                                               commit_2);
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty([this] { QuitLoop(); });

  RunLoopUntilIdle();
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

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(callback::SetWhenCalled(&called));

  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_TRUE(resolver.IsEmpty());

  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());

  std::unique_ptr<const storage::Commit> commit;
  page_storage_->GetCommit(
      ids[0],
      ::callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  ASSERT_TRUE(commit);

  std::vector<storage::Entry> content_vector = GetCommitContents(*commit);
  // Entries are ordered by keys
  ASSERT_EQ(2u, content_vector.size());
  EXPECT_EQ("key2", content_vector[0].key);
  std::string value;
  EXPECT_TRUE(GetValue(content_vector[0].object_identifier, &value));
  EXPECT_EQ("val2.1", value);
  EXPECT_EQ("key3", content_vector[1].key);
  EXPECT_TRUE(GetValue(content_vector[1].object_identifier, &value));
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

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.set_on_empty([this] { QuitLoop(); });
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsEmpty());
  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
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

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_2));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.set_on_empty(callback::SetWhenCalled(&called));
  resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  async::PostTask(dispatcher(), [&resolver] {
    resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  });

  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_TRUE(resolver.IsEmpty());
  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
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
  storage::fake::FakePageStorage page_storage(dispatcher(), "page_id");

  int get_next_count = 0;
  bool on_empty_called;
  MergeResolver resolver([] {}, &environment_, &page_storage,
                         std::make_unique<test::TestBackoff>(&get_next_count));
  resolver.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  auto strategy = std::make_unique<RecordingTestStrategy>();
  strategy->SetOnMerge([this] { QuitLoop(); });
  resolver.SetMergeStrategy(std::move(strategy));

  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);

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

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage.GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), merge_1));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), merge_2));

  page_storage.SetDropCommitNotifications(false);

  storage::CommitWatcher* watcher = &resolver;
  watcher->OnNewCommits({}, storage::ChangeSource::CLOUD);

  // Note we can't use "RunLoopUntilIdle()" because the FakePageStorage delays
  // before inserting tasks into the message loop.
  RunLoopFor(zx::sec(5));

  EXPECT_GT(get_next_count, 0);
}

TEST_F(MergeResolverTest, AutomaticallyMergeIdenticalCommits) {
  // Set up conflict
  storage::CommitId commit_1 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 = CreateCommit(
      storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_1));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_2));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.set_on_empty(callback::SetWhenCalled(&called));
  auto merge_strategy = std::make_unique<RecordingTestStrategy>();
  auto merge_strategy_ptr = merge_strategy.get();
  resolver.SetMergeStrategy(std::move(merge_strategy));

  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_TRUE(resolver.IsEmpty());
  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());
  EXPECT_EQ(0u, merge_strategy_ptr->merge_calls);
}

TEST_F(MergeResolverTest, NoConflictCallback_ConflictsResolved) {
  // Set up conflict.
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "bar"));
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(MakeQuitTaskOnce());

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());

  size_t callback_calls = 0;
  auto conflicts_resolved_callback = [&resolver, &callback_calls]() {
    EXPECT_TRUE(resolver.IsEmpty());
    callback_calls++;
  };
  ConflictResolutionWaitStatus wait_status;
  resolver.RegisterNoConflictCallback(
      callback::Capture(conflicts_resolved_callback, &wait_status));
  resolver.RegisterNoConflictCallback(
      callback::Capture(conflicts_resolved_callback, &wait_status));

  // Check that the callback was called 2 times.
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(2u, callback_calls);
  EXPECT_EQ(ConflictResolutionWaitStatus::CONFLICTS_RESOLVED, wait_status);

  ids.clear();
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(1u, ids.size());

  callback_calls = 0;
  CreateCommit(ids[0], AddKeyValueToJournal("foo", "baw"));
  CreateCommit(ids[0], AddKeyValueToJournal("foo", "bat"));
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsEmpty());

  // Check that callback wasn't called (callback queue cleared after all the
  // callbacks in it were called).
  RunLoopFor(zx::sec(10));
  EXPECT_EQ(0u, callback_calls);
}

TEST_F(MergeResolverTest, NoConflictCallback_NoConflicts) {
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(MakeQuitTaskOnce());

  size_t callback_calls = 0;
  auto conflicts_resolved_callback = [&resolver, &callback_calls]() {
    EXPECT_TRUE(resolver.IsEmpty());
    callback_calls++;
  };
  ConflictResolutionWaitStatus wait_status;
  resolver.RegisterNoConflictCallback(
      callback::Capture(conflicts_resolved_callback, &wait_status));

  // Check that the callback was called 1 times.
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_EQ(1u, callback_calls);
  EXPECT_EQ(ConflictResolutionWaitStatus::NO_CONFLICTS, wait_status);
}

TEST_F(MergeResolverTest, HasUnfinishedMerges) {
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<test::TestBackoff>(nullptr));
  resolver.SetMergeStrategy(nullptr);
  resolver.set_on_empty([this] { QuitLoop(); });
  RunLoopUntilIdle();
  EXPECT_FALSE(resolver.HasUnfinishedMerges());

  // Set up conflict.
  storage::CommitId commit_1 = CreateCommit(storage::kFirstPageCommitId,
                                            AddKeyValueToJournal("foo", "bar"));
  storage::CommitId commit_2 = CreateCommit(storage::kFirstPageCommitId,
                                            AddKeyValueToJournal("foo", "baz"));

  bool called;
  storage::Status status;
  std::vector<storage::CommitId> ids;
  page_storage_->GetHeadCommitIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &ids));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, status);
  EXPECT_EQ(2u, ids.size());

  std::unique_ptr<VerifyingMergeStrategy> strategy =
      std::make_unique<VerifyingMergeStrategy>(
          dispatcher(), commit_1, commit_2,
          storage::kFirstPageCommitId.ToString());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty([this] { QuitLoop(); });

  RunLoopUntilIdle();
  // VerifyingResolver tells MergeResolver that the conflict is finished, but
  // doesn't touch storage. Thus, there is still two head commits at this point:
  // MergeResolver should be empty (no merge in progress), but a merge should be
  // pending still.
  EXPECT_TRUE(resolver.IsEmpty());
  EXPECT_TRUE(resolver.HasUnfinishedMerges());

  std::unique_ptr<LastOneWinsMergeStrategy> new_strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  resolver.SetMergeStrategy(std::move(new_strategy));
  // TODO(LE-465): Simplify this so it's less brittle and easier to understand.
  // The resolver will find the conflict in the first run, then enqueue
  // async tasks to actually finish the merge, then the "on_empty" callback
  // will quit the loop, so RunLoopUntilIdle will stop.
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsEmpty());
  // This second runs the tasks enqueued by the first.
  RunLoopUntilIdle();
  EXPECT_FALSE(resolver.HasUnfinishedMerges());
}

}  // namespace
}  // namespace ledger
