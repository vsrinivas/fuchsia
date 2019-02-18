// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/merging/merge_resolver.h"

#include <string>
#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/backoff/testing/test_backoff.h>
#include <lib/callback/cancellable_helper.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fit/function.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/fxl/macros.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/merging/last_one_wins_merge_strategy.h"
#include "peridot/bin/ledger/app/merging/test_utils.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/encryption/primitives/hash.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
namespace {

using ::testing::UnorderedElementsAre;

class FakePageStorageImpl : public storage::PageStorageEmptyImpl {
 public:
  FakePageStorageImpl(std::unique_ptr<storage::PageStorage> page_storage)
      : storage_(std::move(page_storage)) {}

  void MarkCommitContentsUnavailable(storage::CommitIdView commit_id) {
    removed_commit_ids_.insert(commit_id.ToString());
  }

  void GetHeadCommitIds(
      fit::function<void(storage::Status, std::vector<storage::CommitId>)>
          callback) override {
    storage_->GetHeadCommitIds(std::move(callback));
  }

  void GetCommit(storage::CommitIdView commit_id,
                 fit::function<void(storage::Status,
                                    std::unique_ptr<const storage::Commit>)>
                     callback) override {
    storage_->GetCommit(commit_id, std::move(callback));
  }

  storage::Status AddCommitWatcher(storage::CommitWatcher* watcher) override {
    return storage_->AddCommitWatcher(watcher);
  }

  storage::Status RemoveCommitWatcher(
      storage::CommitWatcher* watcher) override {
    return storage_->RemoveCommitWatcher(watcher);
  }

  void GetObject(storage::ObjectIdentifier object_identifier, Location location,
                 fit::function<void(storage::Status,
                                    std::unique_ptr<const storage::Object>)>
                     callback) override {
    storage_->GetObject(std::move(object_identifier), location,
                        std::move(callback));
  }

  std::unique_ptr<storage::Journal> StartCommit(
      const storage::CommitId& commit_id) override {
    return storage_->StartCommit(commit_id);
  }

  std::unique_ptr<storage::Journal> StartMergeCommit(
      const storage::CommitId& left, const storage::CommitId& right) override {
    return storage_->StartMergeCommit(left, right);
  }

  void CommitJournal(std::unique_ptr<storage::Journal> journal,
                     fit::function<void(storage::Status,
                                        std::unique_ptr<const storage::Commit>)>
                         callback) override {
    storage_->CommitJournal(std::move(journal), std::move(callback));
  }

  void AddObjectFromLocal(
      storage::ObjectType object_type,
      std::unique_ptr<storage::DataSource> data_source,
      fit::function<void(storage::Status, storage::ObjectIdentifier)> callback)
      override {
    storage_->AddObjectFromLocal(object_type, std::move(data_source),
                                 std::move(callback));
  }

  void GetCommitContents(
      const storage::Commit& commit, std::string min_key,
      fit::function<bool(storage::Entry)> on_next,
      fit::function<void(storage::Status)> on_done) override {
    storage_->GetCommitContents(commit, std::move(min_key), std::move(on_next),
                                std::move(on_done));
  }

  void GetCommitContentsDiff(
      const storage::Commit& base_commit, const storage::Commit& other_commit,
      std::string min_key,
      fit::function<bool(storage::EntryChange)> on_next_diff,
      fit::function<void(storage::Status)> on_done) override {
    if (removed_commit_ids_.find(base_commit.GetId()) !=
            removed_commit_ids_.end() ||
        removed_commit_ids_.find(other_commit.GetId()) !=
            removed_commit_ids_.end()) {
      on_done(storage::Status::NOT_CONNECTED_ERROR);
      return;
    }
    storage_->GetCommitContentsDiff(base_commit, other_commit,
                                    std::move(min_key), std::move(on_next_diff),
                                    std::move(on_done));
  }

 private:
  std::set<storage::CommitId> removed_commit_ids_;

  std::unique_ptr<storage::PageStorage> storage_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageStorageImpl);
};

class RecordingTestStrategy : public MergeStrategy {
 public:
  RecordingTestStrategy() {}
  ~RecordingTestStrategy() override {}
  void SetOnError(fit::closure on_error) override {
    this->on_error = std::move(on_error);
  }

  void SetOnMerge(fit::closure on_merge) { on_merge_ = std::move(on_merge); }

  void Merge(storage::PageStorage* /*storage*/, PageManager* /*page_manager*/,
             std::unique_ptr<const storage::Commit> /*head_1*/,
             std::unique_ptr<const storage::Commit> /*head_2*/,
             std::unique_ptr<const storage::Commit> /*ancestor*/,
             fit::function<void(Status)> callback) override {
    this->callback = std::move(callback);
    merge_calls++;
    if (on_merge_) {
      on_merge_();
    }
  }

  void Cancel() override { cancel_calls++; }

  fit::closure on_error;

  fit::function<void(Status)> callback;
  uint32_t merge_calls = 0;
  uint32_t cancel_calls = 0;

 private:
  fit::closure on_merge_;
};

class MergeResolverTest : public TestWithPageStorage {
 public:
  MergeResolverTest() {}
  ~MergeResolverTest() override {}

 protected:
  storage::PageStorage* page_storage() override { return page_storage_.get(); }

  void SetUp() override {
    TestWithPageStorage::SetUp();
    std::unique_ptr<storage::PageStorage> storage;
    ASSERT_TRUE(CreatePageStorage(&storage));
    page_storage_ = std::make_unique<FakePageStorageImpl>(std::move(storage));
  }

  storage::CommitId CreateCommit(
      storage::CommitIdView parent_id,
      fit::function<void(storage::Journal*)> contents) {
    return CreateCommit(page_storage_.get(), parent_id, std::move(contents));
  }

  storage::CommitId CreateCommit(
      storage::PageStorage* storage, storage::CommitIdView parent_id,
      fit::function<void(storage::Journal*)> contents) {
    std::unique_ptr<storage::Journal> journal =
        storage->StartCommit(parent_id.ToString());

    contents(journal.get());
    bool called;
    storage::Status status;
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
      fit::function<void(storage::Journal*)> contents) {
    return CreateMergeCommit(page_storage_.get(), parent_id1, parent_id2,
                             std::move(contents));
  }

  storage::CommitId CreateMergeCommit(
      storage::PageStorage* storage, storage::CommitIdView parent_id1,
      storage::CommitIdView parent_id2,
      fit::function<void(storage::Journal*)> contents) {
    std::unique_ptr<storage::Journal> journal =
        storage->StartMergeCommit(parent_id1.ToString(), parent_id2.ToString());
    contents(journal.get());

    bool called;
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

  std::unique_ptr<FakePageStorageImpl> page_storage_;

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
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.set_on_empty(QuitLoopClosure());

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

class CapturingMergeStrategy : public MergeStrategy {
 public:
  CapturingMergeStrategy() {}
  ~CapturingMergeStrategy() override {}

  void SetOnError(fit::function<void()> /*on_error*/) override {}

  void Merge(storage::PageStorage* /*storage*/, PageManager* /*page_manager*/,
             std::unique_ptr<const storage::Commit> merge_head_1,
             std::unique_ptr<const storage::Commit> merge_head_2,
             std::unique_ptr<const storage::Commit> merge_ancestor,
             fit::function<void(Status)> merge_callback) override {
    head_1 = std::move(merge_head_1);
    head_2 = std::move(merge_head_2);
    ancestor = std::move(merge_ancestor);
    callback = std::move(merge_callback);
  }

  void Cancel() override{};

  std::unique_ptr<const storage::Commit> head_1;
  std::unique_ptr<const storage::Commit> head_2;
  std::unique_ptr<const storage::Commit> ancestor;
  fit::function<void(Status)> callback;

 private:
  FXL_DISALLOW_ASSIGN(CapturingMergeStrategy);
};

TEST_F(MergeResolverTest, CommonAncestor) {
  // Add commits forming the following history graph:
  // (root) -> (1) -> (2) ->  (3)
  //                      \
  //                       -> (4) -> (5)
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
  RunLoopUntilIdle();

  // Set a merge strategy to capture the requested merge.
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<CapturingMergeStrategy> strategy =
      std::make_unique<CapturingMergeStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();

  // Verify that the strategy is asked to merge commits 5 and 3, with 2 as the
  // common ancestor.
  EXPECT_EQ(commit_3, strategy_ptr->head_1->GetId());
  EXPECT_EQ(commit_5, strategy_ptr->head_2->GetId());
  EXPECT_EQ(commit_2, strategy_ptr->ancestor->GetId());

  // Resolve the conflict.
  CreateMergeCommit(strategy_ptr->head_1->GetId(),
                    strategy_ptr->head_2->GetId(),
                    AddKeyValueToJournal("key_foo", "abc"));
  strategy_ptr->callback(Status::OK);
  strategy_ptr->callback = nullptr;
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
                         std::make_unique<backoff::TestBackoff>());
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

TEST_F(MergeResolverTest, LastOneWinsDiffNotAvailable) {
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
  EXPECT_THAT(ids, UnorderedElementsAre(commit_3, commit_5));

  page_storage_->MarkCommitContentsUnavailable(commit_2);

  std::unique_ptr<LastOneWinsMergeStrategy> strategy =
      std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
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
  EXPECT_EQ(2u, ids.size());
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
                         std::make_unique<backoff::TestBackoff>());
  resolver.set_on_empty(QuitLoopClosure());
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
                         std::make_unique<backoff::TestBackoff>());
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
  storage::fake::FakePageStorage page_storage(&environment_, "page_id");

  bool on_empty_called;
  auto backoff = std::make_unique<backoff::TestBackoff>();
  auto backoff_ptr = backoff.get();
  MergeResolver resolver([] {}, &environment_, &page_storage,
                         std::move(backoff));
  resolver.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  auto strategy = std::make_unique<RecordingTestStrategy>();
  strategy->SetOnMerge(QuitLoopClosure());
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

  EXPECT_GT(backoff_ptr->get_next_count, 0);
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
                         std::make_unique<backoff::TestBackoff>());
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
                         std::make_unique<backoff::TestBackoff>());
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
                         std::make_unique<backoff::TestBackoff>());
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
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<CapturingMergeStrategy> strategy =
      std::make_unique<CapturingMergeStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();
  EXPECT_FALSE(resolver.HasUnfinishedMerges());

  // Set up a conflict and verify that HasUnfinishedMerges() returns true.
  storage::CommitId commit_1 = CreateCommit(storage::kFirstPageCommitId,
                                            AddKeyValueToJournal("foo", "bar"));
  storage::CommitId commit_2 = CreateCommit(storage::kFirstPageCommitId,
                                            AddKeyValueToJournal("foo", "baz"));
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.HasUnfinishedMerges());

  // Resolve the conflict and verify that HasUnfinishedMerges() returns false.
  ASSERT_TRUE(strategy_ptr->head_1);
  ASSERT_TRUE(strategy_ptr->head_2);
  ASSERT_TRUE(strategy_ptr->ancestor);
  ASSERT_TRUE(strategy_ptr->callback);
  CreateMergeCommit(strategy_ptr->head_1->GetId(),
                    strategy_ptr->head_2->GetId(),
                    AddKeyValueToJournal("key3", "val3.0"));
  strategy_ptr->callback(Status::OK);
  strategy_ptr->callback = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(resolver.HasUnfinishedMerges());
}

}  // namespace
}  // namespace ledger
