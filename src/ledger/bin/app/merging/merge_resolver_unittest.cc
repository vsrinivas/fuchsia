// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/merge_resolver.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/merging/last_one_wins_merge_strategy.h"
#include "src/ledger/bin/app/merging/test_utils.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/backoff/testing/test_backoff.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {
namespace {

using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::UnorderedElementsAre;

std::vector<storage::CommitId> ToCommitIds(
    const std::vector<std::unique_ptr<const storage::Commit>>& commits) {
  std::vector<storage::CommitId> ids;
  ids.reserve(commits.size());
  for (const auto& commit : commits) {
    ids.push_back(commit->GetId());
  }
  return ids;
}

class FakePageStorageImpl : public storage::PageStorageEmptyImpl {
 public:
  explicit FakePageStorageImpl(std::unique_ptr<storage::PageStorage> page_storage)
      : storage_(std::move(page_storage)) {}
  FakePageStorageImpl(const FakePageStorageImpl&) = delete;
  FakePageStorageImpl& operator=(const FakePageStorageImpl&) = delete;

  void MarkCommitContentsUnavailable(storage::CommitIdView commit_id) {
    removed_commit_ids_.insert(convert::ToString(commit_id));
  }

  Status GetHeadCommits(
      std::vector<std::unique_ptr<const storage::Commit>>* head_commits) override {
    return storage_->GetHeadCommits(head_commits);
  }

  void GetMergeCommitIds(
      storage::CommitIdView parent1, storage::CommitIdView parent2,
      fit::function<void(Status, std::vector<storage::CommitId>)> callback) override {
    storage_->GetMergeCommitIds(parent1, parent2, std::move(callback));
  }

  void GetCommit(
      storage::CommitIdView commit_id,
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback) override {
    storage_->GetCommit(commit_id, std::move(callback));
  }

  void AddCommitWatcher(storage::CommitWatcher* watcher) override {
    storage_->AddCommitWatcher(watcher);
  }

  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override {
    storage_->RemoveCommitWatcher(watcher);
  }

  void GetObject(
      storage::ObjectIdentifier object_identifier, Location location,
      fit::function<void(Status, std::unique_ptr<const storage::Object>)> callback) override {
    storage_->GetObject(std::move(object_identifier), location, std::move(callback));
  }

  std::unique_ptr<storage::Journal> StartCommit(
      std::unique_ptr<const storage::Commit> commit) override {
    return storage_->StartCommit(std::move(commit));
  }

  std::unique_ptr<storage::Journal> StartMergeCommit(
      std::unique_ptr<const storage::Commit> left,
      std::unique_ptr<const storage::Commit> right) override {
    return storage_->StartMergeCommit(std::move(left), std::move(right));
  }

  void CommitJournal(
      std::unique_ptr<storage::Journal> journal,
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback) override {
    storage_->CommitJournal(std::move(journal), std::move(callback));
  }

  void AddObjectFromLocal(
      storage::ObjectType object_type, std::unique_ptr<storage::DataSource> data_source,
      storage::ObjectReferencesAndPriority tree_references,
      fit::function<void(Status, storage::ObjectIdentifier)> callback) override {
    storage_->AddObjectFromLocal(object_type, std::move(data_source), std::move(tree_references),
                                 std::move(callback));
  }

  void GetCommitContents(const storage::Commit& commit, std::string min_key,
                         fit::function<bool(storage::Entry)> on_next,
                         fit::function<void(Status)> on_done) override {
    storage_->GetCommitContents(commit, std::move(min_key), std::move(on_next), std::move(on_done));
  }

  void GetCommitContentsDiff(const storage::Commit& base_commit,
                             const storage::Commit& other_commit, std::string min_key,
                             fit::function<bool(storage::EntryChange)> on_next_diff,
                             fit::function<void(Status)> on_done) override {
    if (removed_commit_ids_.find(base_commit.GetId()) != removed_commit_ids_.end() ||
        removed_commit_ids_.find(other_commit.GetId()) != removed_commit_ids_.end()) {
      on_done(Status::NETWORK_ERROR);
      return;
    }
    storage_->GetCommitContentsDiff(base_commit, other_commit, std::move(min_key),
                                    std::move(on_next_diff), std::move(on_done));
  }

 private:
  std::set<storage::CommitId> removed_commit_ids_;

  std::unique_ptr<storage::PageStorage> storage_;
};

class RecordingTestStrategy : public MergeStrategy {
 public:
  RecordingTestStrategy() = default;
  ~RecordingTestStrategy() override = default;
  void SetOnError(fit::closure on_error) override { this->on_error = std::move(on_error); }

  void SetOnMerge(fit::closure on_merge) { on_merge_ = std::move(on_merge); }

  void Merge(storage::PageStorage* storage, ActivePageManager* active_page_manager,
             std::unique_ptr<const storage::Commit> merge_head_1,
             std::unique_ptr<const storage::Commit> merge_head_2,
             std::unique_ptr<const storage::Commit> merge_ancestor,
             fit::function<void(Status)> merge_callback) override {
    EXPECT_TRUE(storage::Commit::TimestampOrdered(merge_head_1, merge_head_2));
    storage_ = storage;
    active_page_manager_ = active_page_manager;
    callback = std::move(merge_callback);
    head_1 = std::move(merge_head_1);
    head_2 = std::move(merge_head_2);
    ancestor = std::move(merge_ancestor);
    merge_calls++;
    if (on_merge_) {
      on_merge_();
    }
  }

  void Forward(MergeStrategy* strategy) {
    strategy->Merge(storage_, active_page_manager_, std::move(head_1), std::move(head_2),
                    std::move(ancestor), std::move(callback));
  }

  void Cancel() override { cancel_calls++; }

  fit::closure on_error;

  std::unique_ptr<const storage::Commit> head_1;
  std::unique_ptr<const storage::Commit> head_2;
  std::unique_ptr<const storage::Commit> ancestor;
  fit::function<void(Status)> callback;

  uint32_t merge_calls = 0;
  uint32_t cancel_calls = 0;

 private:
  storage::PageStorage* storage_;
  ActivePageManager* active_page_manager_;
  fit::closure on_merge_;
};

class MergeResolverTest : public TestWithPageStorage {
 public:
  MergeResolverTest() = default;
  MergeResolverTest(const MergeResolverTest&) = delete;
  MergeResolverTest& operator=(const MergeResolverTest&) = delete;
  ~MergeResolverTest() override = default;

 protected:
  storage::PageStorage* page_storage() override { return page_storage_.get(); }

  void SetUp() override {
    TestWithPageStorage::SetUp();
    std::unique_ptr<storage::PageStorage> storage;
    ASSERT_TRUE(CreatePageStorage(&storage));
    page_storage_ = std::make_unique<FakePageStorageImpl>(std::move(storage));
  }

  storage::CommitId CreateCommit(storage::CommitIdView parent_id,
                                 fit::function<void(storage::Journal*)> contents) {
    return CreateCommit(page_storage_.get(), parent_id, std::move(contents));
  }

  storage::CommitId CreateCommit(storage::PageStorage* storage, storage::CommitIdView parent_id,
                                 fit::function<void(storage::Journal*)> contents) {
    Status status;
    bool called;
    std::unique_ptr<const storage::Commit> base;
    storage->GetCommit(parent_id,
                       callback::Capture(callback::SetWhenCalled(&called), &status, &base));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    std::unique_ptr<storage::Journal> journal = storage->StartCommit(std::move(base));

    contents(journal.get());
    std::unique_ptr<const storage::Commit> commit;
    storage->CommitJournal(std::move(journal),
                           callback::Capture(callback::SetWhenCalled(&called), &status, &commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);
    return commit->GetId();
  }

  storage::CommitId CreateMergeCommit(storage::CommitIdView parent_id1,
                                      storage::CommitIdView parent_id2,
                                      fit::function<void(storage::Journal*)> contents) {
    return CreateMergeCommit(page_storage_.get(), parent_id1, parent_id2, std::move(contents));
  }

  storage::CommitId CreateMergeCommit(storage::PageStorage* storage,
                                      storage::CommitIdView parent_id1,
                                      storage::CommitIdView parent_id2,
                                      fit::function<void(storage::Journal*)> contents) {
    Status status;
    bool called;
    std::unique_ptr<const storage::Commit> base1;
    storage->GetCommit(parent_id1,
                       callback::Capture(callback::SetWhenCalled(&called), &status, &base1));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    std::unique_ptr<const storage::Commit> base2;
    storage->GetCommit(parent_id2,
                       callback::Capture(callback::SetWhenCalled(&called), &status, &base2));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(status, Status::OK);

    std::unique_ptr<storage::Journal> journal =
        storage->StartMergeCommit(std::move(base1), std::move(base2));
    contents(journal.get());

    Status actual_status;
    std::unique_ptr<const storage::Commit> actual_commit;
    storage->CommitJournal(std::move(journal), callback::Capture(callback::SetWhenCalled(&called),
                                                                 &actual_status, &actual_commit));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);
    EXPECT_EQ(actual_status, Status::OK);
    return actual_commit->GetId();
  }

  std::vector<storage::Entry> GetCommitContents(const storage::Commit& commit) {
    Status status;
    std::vector<storage::Entry> result;
    auto on_next = [&result](storage::Entry e) {
      result.push_back(e);
      return true;
    };
    bool called;
    page_storage_->GetCommitContents(commit, "", std::move(on_next),
                                     callback::Capture(callback::SetWhenCalled(&called), &status));
    RunLoopUntilIdle();
    EXPECT_TRUE(called);

    EXPECT_EQ(status, Status::OK);
    return result;
  }

  // Checks that a string represents a valid set of changes: it is sorted and
  // does not contain duplicates.
  bool ValidSet(std::string state) {
    return std::is_sorted(state.begin(), state.end()) &&
           std::adjacent_find(state.begin(), state.end()) == state.end();
  }

  // Merge two sets of changes, represented by sorted strings. Assuming that all
  // changes are represented by unique letters, this checks that the base has
  // exactly the common changes between left and right, and returns a version
  // that includes all the changes of left and right.
  // This is exactly the property we expect merging to verify.
  std::string MergeAsSets(std::string left, std::string right, std::string base) {
    std::string out;
    std::string expected_base;
    EXPECT_TRUE(ValidSet(base));
    EXPECT_TRUE(ValidSet(left));
    EXPECT_TRUE(ValidSet(right));
    std::set_intersection(left.begin(), left.end(), right.begin(), right.end(),
                          std::back_inserter(expected_base));
    EXPECT_EQ(base, expected_base) << " when merging " << left << " and " << right;
    std::set_union(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(out));
    EXPECT_TRUE(ValidSet(out));
    return out;
  }

  std::string GetKeyOrEmpty(const storage::Commit& commit, std::string key) {
    std::vector<storage::Entry> entries = GetCommitContents(commit);
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&key](storage::Entry entry) { return entry.key == key; });
    if (it == entries.end()) {
      return "";
    }
    std::string value;
    EXPECT_TRUE(GetValue(it->object_identifier, &value));
    return value;
  }

  void MergeCommitsAsSets(const storage::Commit& left, const storage::Commit& right,
                          const storage::Commit& base) {
    std::string merge =
        MergeAsSets(GetKeyOrEmpty(left, "k"), GetKeyOrEmpty(right, "k"), GetKeyOrEmpty(base, "k"));
    CreateMergeCommit(left.GetId(), right.GetId(), AddKeyValueToJournal("k", merge));
  }

  std::unique_ptr<FakePageStorageImpl> page_storage_;
};

TEST_F(MergeResolverTest, Empty) {
  // Set up conflict
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "bar"));
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy = std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.SetOnDiscardable(QuitLoopClosure());

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 2u);

  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsDiscardable());

  commits.clear();
  status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 1u);
}

TEST_F(MergeResolverTest, CommonAncestor) {
  // Add commits forming the following history graph:
  // (root) -> (1) -> (2) ->  (3)
  //                      \
  //                       -> (4) -> (5)
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));
  storage::CommitId commit_2 = CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));
  storage::CommitId commit_3 = CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));
  storage::CommitId commit_4 = CreateCommit(commit_2, DeleteKeyFromJournal("key1"));
  storage::CommitId commit_5 = CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));
  RunLoopUntilIdle();

  // Set a merge strategy to capture the requested merge.
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();

  // Verify that the strategy is asked to merge commits 5 and 3, with 2 as the
  // common ancestor.
  EXPECT_EQ(strategy_ptr->head_1->GetId(), commit_3);
  EXPECT_EQ(strategy_ptr->head_2->GetId(), commit_5);
  EXPECT_EQ(strategy_ptr->ancestor->GetId(), commit_2);

  // Resolve the conflict.
  CreateMergeCommit(strategy_ptr->head_1->GetId(), strategy_ptr->head_2->GetId(),
                    AddKeyValueToJournal("key_foo", "abc"));
  strategy_ptr->callback(Status::OK);
  strategy_ptr->callback = nullptr;
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsDiscardable());
}

TEST_F(MergeResolverTest, LastOneWins) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 = CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 = CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 = CreateCommit(commit_2, DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 = CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  auto ids = ToCommitIds(commits);
  EXPECT_THAT(ids, UnorderedElementsAre(commit_3, commit_5));

  bool called;
  std::unique_ptr<LastOneWinsMergeStrategy> strategy = std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.SetOnDiscardable(callback::SetWhenCalled(&called));

  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_TRUE(resolver.IsDiscardable());

  commits.clear();
  status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 1u);

  std::vector<storage::Entry> content_vector = GetCommitContents(*commits[0]);
  // Entries are ordered by keys
  ASSERT_EQ(content_vector.size(), 2u);
  EXPECT_EQ(content_vector[0].key, "key2");
  std::string value;
  EXPECT_TRUE(GetValue(content_vector[0].object_identifier, &value));
  EXPECT_EQ(value, "val2.1");
  EXPECT_EQ(content_vector[1].key, "key3");
  EXPECT_TRUE(GetValue(content_vector[1].object_identifier, &value));
  EXPECT_EQ(value, "val3.0");
}

TEST_F(MergeResolverTest, LastOneWinsDiffNotAvailable) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 = CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 = CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 = CreateCommit(commit_2, DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 = CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_THAT(ToCommitIds(commits), UnorderedElementsAre(commit_3, commit_5));

  page_storage_->MarkCommitContentsUnavailable(commit_2);

  bool called;
  std::unique_ptr<LastOneWinsMergeStrategy> strategy = std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.SetOnDiscardable(callback::SetWhenCalled(&called));

  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_TRUE(resolver.IsDiscardable());
  commits.clear();
  status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 2u);
}

TEST_F(MergeResolverTest, None) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 = CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 = CreateCommit(commit_2, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId commit_4 = CreateCommit(commit_2, DeleteKeyFromJournal("key1"));

  storage::CommitId commit_5 = CreateCommit(commit_4, AddKeyValueToJournal("key2", "val2.1"));

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 2u);
  std::vector<storage::CommitId> ids = ToCommitIds(commits);
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_3));
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), commit_5));

  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetOnDiscardable(QuitLoopClosure());
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsDiscardable());
  commits.clear();
  status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 2u);
}

TEST_F(MergeResolverTest, UpdateMidResolution) {
  // Set up conflict
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 = CreateCommit(commit_1, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId commit_3 = CreateCommit(commit_1, AddKeyValueToJournal("key3", "val3.0"));

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 2u);
  EXPECT_THAT(ToCommitIds(commits), UnorderedElementsAre(commit_2, commit_3));

  bool called;
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetOnDiscardable(callback::SetWhenCalled(&called));
  resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  async::PostTask(dispatcher(), [&resolver] {
    resolver.SetMergeStrategy(std::make_unique<LastOneWinsMergeStrategy>());
  });

  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  EXPECT_TRUE(resolver.IsDiscardable());
  commits.clear();
  status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 1u);
}

// Merge of merges backoff is only triggered when commits are coming from sync.
// To test this, we need to create conflicts and make it as if they are not
// created locally. This is done by preventing commit notifications for new
// commits, then issuing manually a commit notification "from sync". As this
// implies using a fake PageStorage, we don't test the resolution itself, only
// that backoff is triggered correctly.
TEST_F(MergeResolverTest, WaitOnMergeOfMerges) {
  storage::fake::FakePageStorage page_storage(&environment_, "page_id");

  bool on_discardable_called;
  auto backoff = std::make_unique<backoff::TestBackoff>();
  auto backoff_ptr = backoff.get();
  MergeResolver resolver([] {}, &environment_, &page_storage, std::move(backoff));
  resolver.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  auto strategy = std::make_unique<RecordingTestStrategy>();
  strategy->SetOnMerge(QuitLoopClosure());
  resolver.SetMergeStrategy(std::move(strategy));

  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  page_storage.SetDropCommitNotifications(true);

  // Set up conflict
  storage::CommitId commit_0 =
      CreateCommit(&page_storage, storage::kFirstPageCommitId, [](storage::Journal*) {});

  storage::CommitId commit_1 =
      CreateCommit(&page_storage, commit_0, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_2 =
      CreateCommit(&page_storage, commit_0, AddKeyValueToJournal("key1", "val1.0"));

  storage::CommitId commit_3 =
      CreateCommit(&page_storage, commit_0, AddKeyValueToJournal("key2", "val2.0"));

  storage::CommitId merge_1 =
      CreateMergeCommit(&page_storage, commit_1, commit_3, AddKeyValueToJournal("key3", "val3.0"));

  storage::CommitId merge_2 =
      CreateMergeCommit(&page_storage, commit_2, commit_3, AddKeyValueToJournal("key3", "val3.0"));

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage.GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 2u);
  EXPECT_THAT(ToCommitIds(commits), UnorderedElementsAre(merge_1, merge_2));

  page_storage.SetDropCommitNotifications(false);

  storage::CommitWatcher* watcher = &resolver;
  watcher->OnNewCommits({}, storage::ChangeSource::CLOUD);

  // Note we can't use "RunLoopUntilIdle()" because the FakePageStorage delays
  // before inserting tasks into the message loop.
  RunLoopFor(zx::sec(5));

  EXPECT_GT(backoff_ptr->get_next_count, 0);
}

TEST_F(MergeResolverTest, NoConflictCallback_ConflictsResolved) {
  // Set up conflict.
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "bar"));
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy = std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.SetOnDiscardable(MakeQuitTaskOnce());

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 2u);

  RunLoopUntilIdle();

  size_t callback_calls = 0;
  auto conflicts_resolved_callback = [&resolver, &callback_calls]() {
    EXPECT_TRUE(resolver.IsDiscardable());
    callback_calls++;
  };
  ConflictResolutionWaitStatus wait_status;
  resolver.RegisterNoConflictCallback(callback::Capture(conflicts_resolved_callback, &wait_status));
  resolver.RegisterNoConflictCallback(callback::Capture(conflicts_resolved_callback, &wait_status));

  // Check that the callback was called 2 times.
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsDiscardable());
  EXPECT_EQ(callback_calls, 2u);
  EXPECT_EQ(wait_status, ConflictResolutionWaitStatus::CONFLICTS_RESOLVED);

  commits.clear();
  status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 1u);

  callback_calls = 0;
  CreateCommit(commits[0]->GetId(), AddKeyValueToJournal("foo", "baw"));
  CreateCommit(commits[0]->GetId(), AddKeyValueToJournal("foo", "bat"));
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsDiscardable());

  // Check that callback wasn't called (callback queue cleared after all the
  // callbacks in it were called).
  RunLoopFor(zx::sec(10));
  EXPECT_EQ(callback_calls, 0u);
}

TEST_F(MergeResolverTest, NoConflictCallback_NoConflicts) {
  CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  std::unique_ptr<LastOneWinsMergeStrategy> strategy = std::make_unique<LastOneWinsMergeStrategy>();
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  resolver.SetMergeStrategy(std::move(strategy));
  resolver.SetOnDiscardable(MakeQuitTaskOnce());

  size_t callback_calls = 0;
  auto conflicts_resolved_callback = [&resolver, &callback_calls]() {
    EXPECT_TRUE(resolver.IsDiscardable());
    callback_calls++;
  };
  ConflictResolutionWaitStatus wait_status;
  resolver.RegisterNoConflictCallback(callback::Capture(conflicts_resolved_callback, &wait_status));

  // Check that the callback was called 1 times.
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.IsDiscardable());
  EXPECT_EQ(callback_calls, 1u);
  EXPECT_EQ(wait_status, ConflictResolutionWaitStatus::NO_CONFLICTS);
}

TEST_F(MergeResolverTest, HasUnfinishedMerges) {
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();
  EXPECT_FALSE(resolver.HasUnfinishedMerges());

  // Set up a conflict and verify that HasUnfinishedMerges() returns true.
  storage::CommitId commit_1 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "bar"));
  storage::CommitId commit_2 =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("foo", "baz"));
  RunLoopUntilIdle();
  EXPECT_TRUE(resolver.HasUnfinishedMerges());

  // Resolve the conflict and verify that HasUnfinishedMerges() returns false.
  ASSERT_TRUE(strategy_ptr->head_1);
  ASSERT_TRUE(strategy_ptr->head_2);
  ASSERT_TRUE(strategy_ptr->ancestor);
  ASSERT_TRUE(strategy_ptr->callback);
  CreateMergeCommit(strategy_ptr->head_1->GetId(), strategy_ptr->head_2->GetId(),
                    AddKeyValueToJournal("key3", "val3.0"));
  strategy_ptr->callback(Status::OK);
  strategy_ptr->callback = nullptr;
  RunLoopUntilIdle();
  EXPECT_FALSE(resolver.HasUnfinishedMerges());
}

// The commit graph is as follows:
//     (root)
//     /  |  \
//   (A) (B) (C)
//    | X \  /
//    |/ \ (E)
//   (D)  \ |
//         (F)
// (D) and (F) are both heads, with (D) containing the changes (A) and (B), and
// (F) containing (A), (B), (C). This should merge to the content of (F) without
// invoking the conflict resolver.
TEST_F(MergeResolverTest, MergeSubsets) {
  storage::CommitId commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "a"));
  storage::CommitId commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "b"));
  storage::CommitId commit_c =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "c"));
  storage::CommitId commit_d =
      CreateMergeCommit(commit_a, commit_b, AddKeyValueToJournal("k", "d"));
  storage::CommitId commit_e =
      CreateMergeCommit(commit_b, commit_c, AddKeyValueToJournal("k", "e"));
  storage::CommitId commit_f =
      CreateMergeCommit(commit_a, commit_e, AddKeyValueToJournal("k", "f"));
  RunLoopUntilIdle();

  // Set a merge strategy to check that no merge is requested
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();

  // Verify that the strategy has not been called
  EXPECT_FALSE(strategy_ptr->callback);

  // Verify there is only one head with the content of commit F
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  ASSERT_EQ(commits.size(), 1u);

  bool called;
  std::unique_ptr<const storage::Commit> commitptr_f;
  page_storage_->GetCommit(
      commit_f, callback::Capture(callback::SetWhenCalled(&called), &status, &commitptr_f));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  ASSERT_TRUE(commitptr_f);

  EXPECT_EQ(commitptr_f->GetRootIdentifier(), commits[0]->GetRootIdentifier());
}

// Check that two equivalent commits are merged to a commit with the content of
// one of the two. The commit graph is as follows:
//    (root)
//    |    |
//   (A)  (B)
//    | \/ |
//    | /\ |
//   (C)  (D)
TEST_F(MergeResolverTest, MergeEquivalents) {
  storage::CommitId commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "a"));
  storage::CommitId commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "b"));
  storage::CommitId commit_c =
      CreateMergeCommit(commit_a, commit_b, AddKeyValueToJournal("k", "c"));
  storage::CommitId commit_d =
      CreateMergeCommit(commit_a, commit_b, AddKeyValueToJournal("k", "d"));
  RunLoopUntilIdle();

  // Set a merge strategy to check that no merge is requested
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();

  // Verify that the strategy has not been called
  EXPECT_FALSE(strategy_ptr->callback);

  // Verify there is only one head with the content of commit C or D
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  ASSERT_EQ(commits.size(), 1u);

  bool called;
  std::unique_ptr<const storage::Commit> commitptr_c;
  page_storage_->GetCommit(
      commit_c, callback::Capture(callback::SetWhenCalled(&called), &status, &commitptr_c));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  ASSERT_TRUE(commitptr_c);

  std::unique_ptr<const storage::Commit> commitptr_d;
  page_storage_->GetCommit(
      commit_d, callback::Capture(callback::SetWhenCalled(&called), &status, &commitptr_d));
  RunLoopUntilIdle();
  ASSERT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  ASSERT_TRUE(commitptr_d);

  EXPECT_THAT(commits[0]->GetRootIdentifier(),
              AnyOf(Eq(commitptr_c->GetRootIdentifier()), Eq(commitptr_d->GetRootIdentifier())));
}

// Tests that already existing merges are used
// The commit graph is:
// In this test, the commits have the following structure:
//       (root)
//       /    \
//     (A)    (B)
//      | \  / |
//     (C) \/ (D)
//      |  /\  |
//      | /  \ |
//     (E)    (F)
//      | (G)
//      | /
//     (H)
// and (G) is a merge of (A) and (B)
// Then merging (F) and (H) should be done using (G) as a base.
TEST_F(MergeResolverTest, ReuseExistingMerge) {
  storage::CommitId commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "a"));
  storage::CommitId commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "b"));
  storage::CommitId commit_c = CreateCommit(commit_a, AddKeyValueToJournal("k", "c"));
  storage::CommitId commit_d = CreateCommit(commit_b, AddKeyValueToJournal("k", "d"));
  storage::CommitId commit_e =
      CreateMergeCommit(commit_c, commit_b, AddKeyValueToJournal("k", "e"));
  storage::CommitId commit_f =
      CreateMergeCommit(commit_a, commit_d, AddKeyValueToJournal("k", "f"));
  storage::CommitId commit_g =
      CreateMergeCommit(commit_a, commit_b, AddKeyValueToJournal("k", "g"));
  // commit (H) is necessary because otherwise (G) is a head
  storage::CommitId commit_h =
      CreateMergeCommit(commit_e, commit_g, AddKeyValueToJournal("k", "h"));
  RunLoopUntilIdle();

  // Set a merge strategy.
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();

  // The merge strategy is called once to merge E and F with G as a base
  ASSERT_TRUE(strategy_ptr->callback);
  EXPECT_EQ(strategy_ptr->ancestor->GetId(), commit_g);
  EXPECT_THAT((std::vector<storage::CommitId>{strategy_ptr->head_1->GetId(),
                                              strategy_ptr->head_2->GetId()}),
              UnorderedElementsAre(commit_f, commit_h));

  // Create the merge
  CreateMergeCommit(strategy_ptr->head_1->GetId(), strategy_ptr->head_2->GetId(),
                    AddKeyValueToJournal("k", "merge"));
  strategy_ptr->callback(Status::OK);
  RunLoopUntilIdle();

  // There is only one head now
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  EXPECT_EQ(commits.size(), 1u);
}

// Tests that recursive merge work correctly: they terminate and produce a
// commit that integrates each change once.
// The commit graph is the following:
//     (root)
//    /  |  \
//  (A) (B) (C)
//   | \/ \/ |
//  (D)/\ /\(E)
//   |/  X  \|
//  (F) / \ (G)
//   | /   \ |
//  (H)     (I)
// Then a merge of (H) and (I) will use (A), (B), (C) as a base.
// The merge can proceed in different ways, but will always call the strategy 3
// times. The conflict resolver computes left+right-base on sets represented as
// strings. The final state should be equivalent to "abcde".
TEST_F(MergeResolverTest, RecursiveMerge) {
  storage::CommitId commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "a"));
  storage::CommitId commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "b"));
  storage::CommitId commit_c =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "c"));
  storage::CommitId commit_d = CreateCommit(commit_a, AddKeyValueToJournal("k", "ad"));
  storage::CommitId commit_e = CreateCommit(commit_c, AddKeyValueToJournal("k", "ce"));
  storage::CommitId commit_f =
      CreateMergeCommit(commit_b, commit_d, AddKeyValueToJournal("k", "abd"));
  storage::CommitId commit_g =
      CreateMergeCommit(commit_b, commit_e, AddKeyValueToJournal("k", "bce"));
  storage::CommitId commit_h =
      CreateMergeCommit(commit_f, commit_c, AddKeyValueToJournal("k", "abcd"));
  storage::CommitId commit_i =
      CreateMergeCommit(commit_a, commit_g, AddKeyValueToJournal("k", "abce"));
  RunLoopUntilIdle();

  // Set up a merge strategy.
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();

  // Do three merges, merging values as sets.
  for (int i = 0; i < 3; i++) {
    EXPECT_TRUE(strategy_ptr->callback);
    if (strategy_ptr->callback) {
      MergeCommitsAsSets(*strategy_ptr->head_1, *strategy_ptr->head_2, *strategy_ptr->ancestor);
      strategy_ptr->callback(Status::OK);
    }
    strategy_ptr->callback = nullptr;
    RunLoopUntilIdle();
  }
  EXPECT_FALSE(strategy_ptr->callback);

  std::vector<std::unique_ptr<const storage::Commit>> commits;
  Status status = page_storage_->GetHeadCommits(&commits);
  EXPECT_EQ(status, Status::OK);
  ASSERT_EQ(commits.size(), 1u);

  // Check the value of k in the commit.
  EXPECT_EQ(GetKeyOrEmpty(*commits[0], "k"), "abcde");
}

// Check that merges are done in timestamp order: in a merge with three bases,
// the two commits with highest timestamp are used first.  The commit graph is
// the following: we add the commits (U) and (V) to ensure that (B) and (C) have
// a higher generation than (A), so we can detect if merging is done in
// generation order instead of timestamp order.
//     (root)
//    /  |  \
//   |  (U) (V)
//   |   |   |
//  (A) (B) (C)
//   | \/ \/ |
//  (D)/\ /\(E)
//   |/  X  \|
//  (F) / \ (G)
//   | /   \ |
//  (H)     (I)
// We do not test the order of subsequent merges.
TEST_F(MergeResolverTest, RecursiveMergeOrder) {
  storage::CommitId commit_u =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "u"));
  storage::CommitId commit_v =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "v"));

  // Commit a, b and c can be done in any order.
  storage::CommitId commit_b = CreateCommit(commit_u, AddKeyValueToJournal("k", "bu"));
  // Ensure time advances between the commits
  RunLoopFor(zx::duration(1));
  storage::CommitId commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "a"));
  RunLoopFor(zx::duration(1));
  storage::CommitId commit_c = CreateCommit(commit_v, AddKeyValueToJournal("k", "cv"));

  storage::CommitId commit_d = CreateCommit(commit_a, AddKeyValueToJournal("k", "ad"));
  storage::CommitId commit_e = CreateCommit(commit_c, AddKeyValueToJournal("k", "cev"));
  storage::CommitId commit_f =
      CreateMergeCommit(commit_b, commit_d, AddKeyValueToJournal("k", "abdu"));
  storage::CommitId commit_g =
      CreateMergeCommit(commit_b, commit_e, AddKeyValueToJournal("k", "bcev"));
  storage::CommitId commit_h =
      CreateMergeCommit(commit_f, commit_c, AddKeyValueToJournal("k", "abcduv"));
  storage::CommitId commit_i =
      CreateMergeCommit(commit_a, commit_g, AddKeyValueToJournal("k", "abceuv"));
  RunLoopUntilIdle();

  // Set up a merge strategy
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));
  RunLoopUntilIdle();

  // Inspect the first merge. It should be between b and a.
  ASSERT_TRUE(strategy_ptr->callback);
  EXPECT_EQ(strategy_ptr->ancestor->GetId(), storage::kFirstPageCommitId);
  EXPECT_EQ(commit_b, strategy_ptr->head_1->GetId());
  EXPECT_EQ(commit_a, strategy_ptr->head_2->GetId());
}

// Checks that last-one-wins picks up changes in the right order for recursive
// merges.  When doing recursive merges, the set of commits to be merged is to
// construct the base is known in advance, so the order should be completely
// determinstic: keys coming from newer commits always win against older
// commits, even with intermediate merges.
//
// The commit graph is the following. The goal is to observe the construction of
// the merge base (we are not interested in the final merge), so we construct
// commits (H) and (I) whose set of common ancestors is {(A), (B), (C)}.
//
//     (root)
//    /  |  \
//  (A) (B) (C)
//   | \/ \/ |
//  (D)/\ /\(E)
//   |/  X  \|
//  (F) / \ (G)
//   | /   \ |
//  (H)     (I)
//
// The merge can proceed in different ways: they may be intervening merges that
// are done without calling the conflict resolver because one commit contains a
// subset of the changes of the other. This test only checks the merges that
// involve the conflict resolver. There are three such merges: one between A and
// B, one between a merge of A and B, and C, and one between commits equivalent
// to H and I.
//
// At the time of writing this comment, the actual sequence of merges is the
// following (assuming D < E in timestamp order):
// - Try to merge H and I. The set of ancestors is {A, B, C}
//     - Merge A and B to J, calling the LastOneWinsStrategy
// - Try to merge J and H (they are the two oldest heads). This is an automatic
//   merge to K, with the same content as H.
// - Try to merge K and I. The set of ancestors is still {A, B, C}
//     - A and B are already merged to J
//     - Merge J and C to L, calling the LastOneWinsStrategy
// - Try to merge K and L. This is an automatic merge to M, with the same
//   content as H.
// - Try to merge M and I. The set of ancestors is {A, B, C}
//     - A and B are already merged to J
//     - J and C are already merged to L.
//     - Merge M and I (identical to H and I) with ancestor L.
TEST_F(MergeResolverTest, RecursiveMergeLastOneWins) {
  // Ensure that A, B, C are in chronological order
  // We insert a key k1 in A, B and C. The value in C should win.
  // We also insert a key k2 in A and B. If A and C are merged first, the value
  // in A will be "refreshed" and be considered as recent as C, and will win
  // against the value in B. We check that this does not happen.
  storage::CommitId commit_a = CreateCommit(storage::kFirstPageCommitId, [this](auto journal) {
    AddKeyValueToJournal("k1", "a")(journal);
    AddKeyValueToJournal("k2", "a")(journal);
  });
  RunLoopFor(zx::duration(1));
  storage::CommitId commit_b = CreateCommit(storage::kFirstPageCommitId, [this](auto journal) {
    AddKeyValueToJournal("k1", "b")(journal);
    AddKeyValueToJournal("k2", "b")(journal);
  });
  RunLoopFor(zx::duration(1));
  storage::CommitId commit_c =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k1", "c"));

  // Build the rest of the graph. We add values to generate changes.
  storage::CommitId commit_d = CreateCommit(commit_a, AddKeyValueToJournal("k", "d"));
  storage::CommitId commit_e = CreateCommit(commit_c, AddKeyValueToJournal("k", "e"));
  storage::CommitId commit_f = CreateMergeCommit(commit_b, commit_d, [](auto journal) {});
  storage::CommitId commit_g = CreateMergeCommit(commit_b, commit_e, [](auto journal) {});
  storage::CommitId commit_h = CreateMergeCommit(commit_f, commit_c, [](auto journal) {});
  storage::CommitId commit_i = CreateMergeCommit(commit_a, commit_g, [](auto journal) {});
  RunLoopUntilIdle();

  // Set up a merge strategy.
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));

  // Set up a last one wins strategy to forward merges to.
  LastOneWinsMergeStrategy last_one_wins_strategy;

  // Do two merges using last-one-wins. Check that they are merges of A and B
  // (generating a commit AB whose id we cannot recover), then of AB and C.
  RunLoopUntilIdle();
  ASSERT_TRUE(strategy_ptr->callback);
  EXPECT_EQ(strategy_ptr->head_1->GetId(), commit_a);
  EXPECT_EQ(strategy_ptr->head_2->GetId(), commit_b);
  EXPECT_EQ(strategy_ptr->ancestor->GetId(), storage::kFirstPageCommitId);
  strategy_ptr->Forward(&last_one_wins_strategy);

  RunLoopUntilIdle();
  ASSERT_TRUE(strategy_ptr->callback);
  EXPECT_EQ(strategy_ptr->head_2->GetId(), commit_c);
  EXPECT_EQ(strategy_ptr->ancestor->GetId(), storage::kFirstPageCommitId);
  // Check that the first head for the second merge holds the correct values.
  EXPECT_EQ(GetKeyOrEmpty(*strategy_ptr->head_1, "k1"), "b");
  EXPECT_EQ(GetKeyOrEmpty(*strategy_ptr->head_1, "k2"), "b");
  strategy_ptr->Forward(&last_one_wins_strategy);

  // Inspect the last merge: its base is the merge of A, B and C.
  RunLoopUntilIdle();
  ASSERT_TRUE(strategy_ptr->callback);

  // Check if the ancestor is the one we expect.
  EXPECT_EQ(GetKeyOrEmpty(*strategy_ptr->ancestor, "k1"), "c");
  EXPECT_EQ(GetKeyOrEmpty(*strategy_ptr->ancestor, "k2"), "b");
}

// Identical change commits should not be considered equivalent.
// This creates two commits with identical contents, and check that the conflict
// resolver is called anyway.
TEST_F(MergeResolverTest, DoNotAutoMergeIdenticalCommits) {
  storage::CommitId commit_a =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "v"));
  storage::CommitId commit_b =
      CreateCommit(storage::kFirstPageCommitId, AddKeyValueToJournal("k", "v"));

  // Set up a merge strategy.
  MergeResolver resolver([] {}, &environment_, page_storage_.get(),
                         std::make_unique<backoff::TestBackoff>());
  std::unique_ptr<RecordingTestStrategy> strategy = std::make_unique<RecordingTestStrategy>();
  auto strategy_ptr = strategy.get();
  resolver.SetMergeStrategy(std::move(strategy));

  RunLoopUntilIdle();

  // Inspect the first merge
  ASSERT_TRUE(strategy_ptr->callback);
  EXPECT_EQ(strategy_ptr->ancestor->GetId(), storage::kFirstPageCommitId);
  EXPECT_THAT((std::vector<storage::CommitId>{strategy_ptr->head_1->GetId(),
                                              strategy_ptr->head_2->GetId()}),
              UnorderedElementsAre(commit_a, commit_b));
}

}  // namespace
}  // namespace ledger
