// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/commits_children_manager.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/bin/storage/testing/id_and_parent_ids_commit.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/storage/testing/storage_matcher.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/testing/page_sync_empty_impl.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/backoff/exponential_backoff.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/vmo/strings.h"

namespace ledger {
namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsTrue;

using CommitsChildrenManagerTest = TestWithEnvironment;

constexpr int kMinimumConcurrency = 3;
constexpr int kMaximumConcurrency = 30;

// TODO(nathaniel): Deduplicate this duplicated-throughout-a-few-tests utility function into a
// library in... peridot? In the rng namespace?
bool NextBool(rng::Random* random) {
  auto bit_generator = random->NewBitGenerator<bool>();
  return bool(std::uniform_int_distribution(0, 1)(bit_generator));
}

std::unique_ptr<MergeResolver> GetDummyResolver(Environment* environment,
                                                storage::PageStorage* storage) {
  return std::make_unique<MergeResolver>(
      [] {}, environment, storage,
      std::make_unique<ExponentialBackoff>(zx::sec(0), 1u, zx::sec(0),
                                           environment->random()->NewBitGenerator<uint64_t>()));
}

class SubstitutePageStorage final : public storage::PageStorageEmptyImpl {
 public:
  explicit SubstitutePageStorage(std::map<storage::CommitId, std::set<storage::CommitId>> graph,
                                 rng::Random* random, async_dispatcher_t* dispatcher)
      : graph_(std::move(graph)), random_(random), dispatcher_(dispatcher), fail_(-1) {
    for (const auto& [child, parents] : graph_) {
      heads_.insert(child);
      for (const storage::CommitId& parent : parents) {
        heads_.erase(parent);
      }
    }
  }
  ~SubstitutePageStorage() override = default;

  void fail_after_successful_calls(int64_t successful_call_count) { fail_ = successful_call_count; }

 private:
  // storage::PageStorageEmptyImpl:
  storage::Status GetHeadCommits(
      std::vector<std::unique_ptr<const storage::Commit>>* head_commits) override {
    if (fail_ == 0) {
      return storage::Status::INTERNAL_ERROR;
    }
    if (fail_ > 0) {
      fail_--;
    }

    for (const storage::CommitId& head : heads_) {
      head_commits->emplace_back(
          std::make_unique<storage::IdAndParentIdsCommit>(head, graph_[head]));
    }
    return storage::Status::OK;
  }
  void GetCommit(
      storage::CommitIdView commit_id,
      fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback) override {
    auto implementation = [this, commit_id = convert::ToString(commit_id),
                           callback = std::move(callback)] {
      if (fail_ == 0) {
        callback(storage::Status::INTERNAL_ERROR, nullptr);
        return;
      }
      if (fail_ > 0) {
        fail_--;
      }

      if (const auto& it = graph_.find(commit_id); it == graph_.end()) {
        callback(storage::Status::INTERNAL_NOT_FOUND, nullptr);
        return;
      }
      callback(storage::Status::OK,
               std::make_unique<storage::IdAndParentIdsCommit>(commit_id, graph_[commit_id]));
    };

    if (NextBool(random_)) {
      async::PostTask(dispatcher_, std::move(implementation));
    } else {
      implementation();
    }
  }
  void AddCommitWatcher(storage::CommitWatcher* watcher) override {}
  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override {}

  std::set<storage::CommitId> heads_;
  std::map<storage::CommitId, std::set<storage::CommitId>> graph_;
  rng::Random* random_;
  async_dispatcher_t* dispatcher_;

  // The number of calls to complete successfully before terminating calls unsuccessfully. -1 to
  // always complete calls successfully.
  int64_t fail_;
};

class SubstituteInspectablePage : public InspectablePage {
 public:
  explicit SubstituteInspectablePage(std::unique_ptr<ActivePageManager> active_page_manager,
                                     rng::Random* random, async_dispatcher_t* dispatcher)
      : active_page_manager_(std::move(active_page_manager)),
        random_(random),
        dispatcher_(dispatcher) {}

  void NewInspection(fit::function<void(storage::Status status, ExpiringToken token,
                                        ActivePageManager* active_page_manager)>
                         callback) override {
    auto implementation = [this, callback = std::move(callback)] {
      if (active_page_manager_) {
        callback(Status::OK, ExpiringToken(), active_page_manager_.get());
      } else {
        callback(Status::INTERNAL_ERROR, ExpiringToken(), nullptr);
      }
    };
    if (NextBool(random_)) {
      async::PostTask(dispatcher_, std::move(implementation));
    } else {
      implementation();
    }
  }

 private:
  std::unique_ptr<ActivePageManager> active_page_manager_;
  rng::Random* random_;
  async_dispatcher_t* dispatcher_;
};

TEST_F(CommitsChildrenManagerTest, GetNames) {
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {convert::ToString(storage::kFirstPageCommitId), {}}};
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      graph, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  std::set<std::string> names;

  std::unique_ptr<inspect_deprecated::ChildrenManager> commits_children_manager =
      std::make_unique<CommitsChildrenManager>(dispatcher(), &commits_node, &inspectable_page);
  commits_children_manager->GetNames(Capture(SetWhenCalled(&callback_called), &names));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(names,
              ElementsAre(CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId))));
}

TEST_F(CommitsChildrenManagerTest, ConcurrentGetNames) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency =
      std::uniform_int_distribution(kMinimumConcurrency, kMaximumConcurrency)(bit_generator);
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {convert::ToString(storage::kFirstPageCommitId), {}}};
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      graph, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  size_t callbacks_called = 0;
  std::vector<std::set<std::string>> nameses(concurrency);

  std::unique_ptr<inspect_deprecated::ChildrenManager> commits_children_manager =
      std::make_unique<CommitsChildrenManager>(dispatcher(), &commits_node, &inspectable_page);
  for (size_t index{0}; index < concurrency; index++) {
    commits_children_manager->GetNames(Capture([&] { callbacks_called++; }, &(nameses[index])));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(
      nameses,
      Each(ElementsAre(CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)))));
}

TEST_F(CommitsChildrenManagerTest, Attach) {
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {convert::ToString(storage::kFirstPageCommitId), {}}};
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      graph, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
      ->Attach(CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)),
               Capture(SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_FALSE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(CommitsChildrenManagerTest, AttachAbsentCommit) {
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      std::map<storage::CommitId, std::set<storage::CommitId>>(), environment_.random(),
      test_loop().dispatcher());
  page_storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
      ->Attach(CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)),
               Capture(SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_TRUE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(CommitsChildrenManagerTest, ConcurrentAttach) {
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  storage::CommitId four =
      storage::CommitId("00000000000000000000000000000004", storage::kCommitIdSize);
  storage::CommitId five =
      storage::CommitId("00000000000000000000000000000005", storage::kCommitIdSize);
  storage::CommitId six =
      storage::CommitId("00000000000000000000000000000006", storage::kCommitIdSize);
  storage::CommitId seven =
      storage::CommitId("00000000000000000000000000000007", storage::kCommitIdSize);
  storage::CommitId eight =
      storage::CommitId("00000000000000000000000000000008", storage::kCommitIdSize);
  storage::CommitId nine =
      storage::CommitId("00000000000000000000000000000009", storage::kCommitIdSize);
  // Garbage collection has happened - 5 calls 2 a parent and 4 calls 3 a parent but 2 and 3 are not
  // available.
  //
  //      3
  //      x
  //  2   4
  //   x /
  //    5
  //    |
  //    6
  //   / \
  //  7   8
  //  |
  //  9
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {four, {three}}, {five, {two, four}}, {six, {five}},
      {seven, {six}},  {eight, {six}},      {nine, {seven}},
  };
  std::vector<storage::CommitId> commit_ids;
  commit_ids.reserve(graph.size());
  for (auto& [commit_id, unused_parent_commit_ids] : graph) {
    commit_ids.push_back(commit_id);
  }
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency =
      std::uniform_int_distribution(kMinimumConcurrency, kMaximumConcurrency)(bit_generator);
  std::vector<storage::CommitId> attachment_choices{concurrency};
  for (size_t index = 0; index < concurrency; index++) {
    attachment_choices[index] =
        commit_ids[std::uniform_int_distribution<size_t>(0u, commit_ids.size() - 1)(bit_generator)];
  }

  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      graph, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  size_t callbacks_called = 0;
  std::vector<fit::closure> detachers(concurrency);
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  for (size_t index = 0; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
        ->Attach(CommitIdToDisplayName(attachment_choices[index]),
                 Capture([&] { callbacks_called++; }, &detachers[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(detachers, Each(IsTrue()));

  // We expect that the CommitsChildrenManager under test becomes empty when the last detacher is
  // called.
  for (const auto& detacher : detachers) {
    EXPECT_FALSE(on_discardable_called);
    detacher();
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(CommitsChildrenManagerTest, GetNamesErrorGettingActivePageManager) {
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  std::set<std::string> names;

  std::unique_ptr<inspect_deprecated::ChildrenManager> commits_children_manager =
      std::make_unique<CommitsChildrenManager>(dispatcher(), &commits_node, &inspectable_page);
  commits_children_manager->GetNames(Capture(SetWhenCalled(&callback_called), &names));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(names, IsEmpty());
}

TEST_F(CommitsChildrenManagerTest, GetNamesErrorGettingCommits) {
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      std::map<storage::CommitId, std::set<storage::CommitId>>(), environment_.random(),
      test_loop().dispatcher());
  page_storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  std::set<std::string> names;

  std::unique_ptr<inspect_deprecated::ChildrenManager> commits_children_manager =
      std::make_unique<CommitsChildrenManager>(dispatcher(), &commits_node, &inspectable_page);
  commits_children_manager->GetNames(Capture(SetWhenCalled(&callback_called), &names));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(names, IsEmpty());
}

TEST_F(CommitsChildrenManagerTest, AttachErrorGettingActivePageManager) {
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
      ->Attach(CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)),
               Capture(SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_TRUE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(CommitsChildrenManagerTest, AttachErrorGettingCommit) {
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      std::map<storage::CommitId, std::set<storage::CommitId>>(), environment_.random(),
      test_loop().dispatcher());
  page_storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
      ->Attach(CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)),
               Capture(SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_TRUE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(CommitsChildrenManagerTest, AttachInvalidName) {
  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
      ->Attach("Definitely not the display string of a commit ID",
               Capture(SetWhenCalled(&callback_called), &detacher));
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  // The CommitsChildrenManager under test did not surrender program control during the call to
  // Attach so it never needed to check its emptiness after regaining program control.
  EXPECT_FALSE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(CommitsChildrenManagerTest, ConcurrentAttachErrorGettingActivePageManager) {
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  storage::CommitId four =
      storage::CommitId("00000000000000000000000000000004", storage::kCommitIdSize);
  storage::CommitId five =
      storage::CommitId("00000000000000000000000000000005", storage::kCommitIdSize);
  storage::CommitId six =
      storage::CommitId("00000000000000000000000000000006", storage::kCommitIdSize);
  storage::CommitId seven =
      storage::CommitId("00000000000000000000000000000007", storage::kCommitIdSize);
  // There's no graph, so it doesn't matter how many nonexistent commits we request, but... sure,
  // try a random selection of... six.
  std::vector<storage::CommitId> commit_ids{two, three, four, five, six, seven};
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency =
      std::uniform_int_distribution(kMinimumConcurrency, kMaximumConcurrency)(bit_generator);
  std::vector<storage::CommitId> attachment_choices{concurrency};
  for (size_t index = 0; index < concurrency; index++) {
    attachment_choices[index] =
        commit_ids[std::uniform_int_distribution<size_t>(0u, commit_ids.size() - 1)(bit_generator)];
  }

  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  size_t callbacks_called = 0;
  std::vector<fit::closure> detachers(concurrency);
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  for (size_t index = 0; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
        ->Attach(CommitIdToDisplayName(attachment_choices[index]),
                 Capture([&] { callbacks_called++; }, &detachers[index]));
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(commits_children_manager.IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
  EXPECT_THAT(detachers, Each(IsTrue()));
  ASSERT_EQ(callbacks_called, concurrency);

  // The accumulated detachers are callable but have no discernible effect.
  for (const auto& detacher : detachers) {
    commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
    detacher();
    RunLoopUntilIdle();
    EXPECT_FALSE(on_discardable_called);
  }
}

TEST_F(CommitsChildrenManagerTest, ConcurrentAttachErrorGettingCommit) {
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  storage::CommitId four =
      storage::CommitId("00000000000000000000000000000004", storage::kCommitIdSize);
  storage::CommitId five =
      storage::CommitId("00000000000000000000000000000005", storage::kCommitIdSize);
  storage::CommitId six =
      storage::CommitId("00000000000000000000000000000006", storage::kCommitIdSize);
  storage::CommitId seven =
      storage::CommitId("00000000000000000000000000000007", storage::kCommitIdSize);
  storage::CommitId eight =
      storage::CommitId("00000000000000000000000000000008", storage::kCommitIdSize);
  storage::CommitId nine =
      storage::CommitId("00000000000000000000000000000009", storage::kCommitIdSize);
  // Garbage collection has happened - 5 calls 2 a parent and 4 calls 3 a parent but 2 and 3 are not
  // available.
  //
  //      3
  //      x
  //  2   4
  //   x /
  //    5
  //    |
  //    6
  //   / \
  //  7   8
  //  |
  //  9
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {four, {three}}, {five, {two, four}}, {six, {five}},
      {seven, {six}},  {eight, {six}},      {nine, {seven}},
  };
  std::vector<storage::CommitId> commit_ids;
  commit_ids.reserve(graph.size());
  for (auto& [commit_id, unused_parent_commit_ids] : graph) {
    commit_ids.push_back(commit_id);
  }
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency = std::uniform_int_distribution<size_t>(kMinimumConcurrency,
                                                             kMaximumConcurrency)(bit_generator);
  std::vector<storage::CommitId> attachment_choices{concurrency};
  for (size_t index = 0; index < concurrency; index++) {
    attachment_choices[index] =
        commit_ids[std::uniform_int_distribution<size_t>(0, commit_ids.size() - 1)(bit_generator)];
  }
  std::set<storage::CommitId> chosen_commit_ids{};
  for (const storage::CommitId& chosen_commit_id : attachment_choices) {
    chosen_commit_ids.insert(chosen_commit_id);
  }
  size_t successful_storage_call_count =
      std::uniform_int_distribution<size_t>(0, chosen_commit_ids.size() - 1)(bit_generator);

  inspect_deprecated::Node commits_node =
      inspect_deprecated::Node(convert::ToString(kCommitsInspectPathComponent));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      graph, environment_.random(), test_loop().dispatcher());
  page_storage->fail_after_successful_calls(successful_storage_call_count);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  size_t callbacks_called = 0;
  std::vector<fit::closure> detachers(concurrency);
  bool on_discardable_called;

  CommitsChildrenManager commits_children_manager{dispatcher(), &commits_node, &inspectable_page};
  commits_children_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  for (size_t index = 0; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&commits_children_manager)
        ->Attach(CommitIdToDisplayName(attachment_choices[index]),
                 Capture([&] { callbacks_called++; }, &detachers[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(detachers, Each(IsTrue()));

  // We expect that the CommitsChildrenManager under test is empty after the last detacher is
  // called. Depending on the randomness with which this test ran it may be empty earlier (it may be
  // empty right now!) but it is only guaranteed to be empty after the last detacher is called.
  for (const auto& detacher : detachers) {
    detacher();
  }

  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

}  // namespace
}  // namespace ledger
