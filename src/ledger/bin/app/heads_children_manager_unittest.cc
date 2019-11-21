// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/heads_children_manager.h"

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
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {
namespace {

using ::testing::Combine;
using ::testing::Each;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Range;
using ::testing::UnorderedElementsAre;
using ::testing::Values;
using ::testing::WithParamInterface;

using HeadsChildrenManagerTest = TestWithEnvironment;

constexpr int kMinimumConcurrency = 2;
constexpr int kMaximumConcurrency = 8;

// Used by this test and associated test substitutes to control whether or not to task-hop at
// various opportunities throughout the test.
enum class Synchrony {
  ASYNCHRONOUS = 0,
  SYNCHRONOUS = 1,
};

// TODO(nathaniel): Deduplicate this duplicated-throughout-a-few-tests utility function.
std::unique_ptr<MergeResolver> GetDummyResolver(Environment* environment,
                                                storage::PageStorage* storage) {
  return std::make_unique<MergeResolver>(
      [] {}, environment, storage,
      std::make_unique<backoff::ExponentialBackoff>(
          zx::sec(0), 1u, zx::sec(0), environment->random()->NewBitGenerator<uint64_t>()));
}

// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=36298): Deduplicate and canonicalize
// this test substitute.
class HeadCommitsSubstitutePageStorage final : public storage::PageStorageEmptyImpl {
 public:
  explicit HeadCommitsSubstitutePageStorage(
      std::map<storage::CommitId, std::set<storage::CommitId>> graph)
      : graph_(std::move(graph)), fail_(-1) {
    for (const auto& [child, parents] : graph_) {
      heads_.insert(child);
      for (const storage::CommitId& parent : parents) {
        heads_.erase(parent);
      }
    }
  }
  ~HeadCommitsSubstitutePageStorage() override = default;

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
  void AddCommitWatcher(storage::CommitWatcher* watcher) override {}
  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override {}

  std::set<storage::CommitId> heads_;
  std::map<storage::CommitId, std::set<storage::CommitId>> graph_;

  // The number of calls to complete successfully before terminating calls unsuccessfully. -1 to
  // always complete calls successfully.
  int64_t fail_;
};

class SubstituteInspectablePage : public InspectablePage {
 public:
  explicit SubstituteInspectablePage(std::unique_ptr<ActivePageManager> active_page_manager,
                                     Synchrony synchrony, async_dispatcher_t* dispatcher)
      : active_page_manager_(std::move(active_page_manager)),
        synchrony_(synchrony),
        dispatcher_(dispatcher) {}

  // InspectablePage:
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
    switch (synchrony_) {
      case Synchrony::ASYNCHRONOUS:
        async::PostTask(dispatcher_, std::move(implementation));
        break;
      case Synchrony::SYNCHRONOUS:
        implementation();
        break;
    }
  }

 private:
  std::unique_ptr<ActivePageManager> active_page_manager_;
  Synchrony synchrony_;
  async_dispatcher_t* dispatcher_;
};

class DummyInspectablePage : public InspectablePage {
 public:
  DummyInspectablePage() = default;
  ~DummyInspectablePage() override = default;

  // InspectablePage:
  void NewInspection(fit::function<void(storage::Status status, ExpiringToken token,
                                        ActivePageManager* active_page_manager)>
                     /*callback*/) override {
    FAIL() << "The system under test must have misbehaved for this method to have been called!";
  }
};

class SynchronyHeadsChildrenManagerTest : public HeadsChildrenManagerTest,
                                          public WithParamInterface<Synchrony> {};
class SynchronyAndConcurrencyHeadsChildrenManagerTest
    : public HeadsChildrenManagerTest,
      public WithParamInterface<std::tuple<size_t, Synchrony>> {};

TEST_P(SynchronyHeadsChildrenManagerTest, GetNames) {
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  std::map<storage::CommitId, std::set<storage::CommitId>> graph{
      {one, {convert::ToString(storage::kFirstPageCommitId)}},
      {two, {convert::ToString(storage::kFirstPageCommitId)}},
      {three, {convert::ToString(storage::kFirstPageCommitId)}}};
  inspect_deprecated::Node heads_node =
      inspect_deprecated::Node(convert::ToString(kHeadsInspectPathComponent));
  std::unique_ptr<HeadCommitsSubstitutePageStorage> page_storage =
      std::make_unique<HeadCommitsSubstitutePageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), GetParam(),
                                             test_loop().dispatcher()};
  bool callback_called;
  std::set<std::string> names;
  bool on_discardable_called;

  HeadsChildrenManager heads_children_manager{dispatcher(), &heads_node, &inspectable_page};
  heads_children_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&heads_children_manager)
      ->GetNames(callback::Capture(callback::SetWhenCalled(&callback_called), &names));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(names, UnorderedElementsAre(CommitIdToDisplayName(one), CommitIdToDisplayName(two),
                                          CommitIdToDisplayName(three)));
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(SynchronyAndConcurrencyHeadsChildrenManagerTest, GetNames) {
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  std::map<storage::CommitId, std::set<storage::CommitId>> graph{
      {one, {convert::ToString(storage::kFirstPageCommitId)}},
      {two, {convert::ToString(storage::kFirstPageCommitId)}},
      {three, {convert::ToString(storage::kFirstPageCommitId)}}};
  size_t concurrency = std::get<0>(GetParam());
  inspect_deprecated::Node heads_node =
      inspect_deprecated::Node(convert::ToString(kHeadsInspectPathComponent));
  std::unique_ptr<HeadCommitsSubstitutePageStorage> page_storage =
      std::make_unique<HeadCommitsSubstitutePageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager),
                                             std::get<1>(GetParam()), test_loop().dispatcher()};
  size_t callbacks_called = 0;
  std::vector<std::set<std::string>> nameses(concurrency);
  size_t on_discardable_calls = 0;

  HeadsChildrenManager heads_children_manager{dispatcher(), &heads_node, &inspectable_page};
  heads_children_manager.SetOnDiscardable([&on_discardable_calls] { on_discardable_calls++; });
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&heads_children_manager)
        ->GetNames(callback::Capture([&] { callbacks_called++; }, &nameses[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(nameses,
              Each(UnorderedElementsAre(CommitIdToDisplayName(one), CommitIdToDisplayName(two),
                                        CommitIdToDisplayName(three))));
  switch (std::get<1>(GetParam())) {
    case Synchrony::ASYNCHRONOUS:
      EXPECT_EQ(on_discardable_calls, 1u);
      break;
    case Synchrony::SYNCHRONOUS:
      // We may have made the calls concurrently (all before a call to |RunLoopUntilIdle|), but if
      // the |NewInspection| method of the |InspectablePage| used by the |HeadsChildrenManager|
      // under test executes its calls synchronously, the |HeadsChildrenManager| under test will
      // dither between emptiness and nonemptiness.
      EXPECT_THAT(on_discardable_calls, Ge(1u));
      EXPECT_THAT(on_discardable_calls, Le(concurrency));
      break;
  }
}

TEST_P(SynchronyHeadsChildrenManagerTest, Attach) {
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  std::map<storage::CommitId, std::set<storage::CommitId>> graph{
      {one, {convert::ToString(storage::kFirstPageCommitId)}},
      {two, {convert::ToString(storage::kFirstPageCommitId)}},
      {three, {convert::ToString(storage::kFirstPageCommitId)}}};
  inspect_deprecated::Node heads_node =
      inspect_deprecated::Node(convert::ToString(kHeadsInspectPathComponent));
  std::unique_ptr<HeadCommitsSubstitutePageStorage> page_storage =
      std::make_unique<HeadCommitsSubstitutePageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), GetParam(),
                                             test_loop().dispatcher()};
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  HeadsChildrenManager heads_children_manager{dispatcher(), &heads_node, &inspectable_page};
  heads_children_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&heads_children_manager)
      ->Attach(CommitIdToDisplayName(two),
               callback::Capture(callback::SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_FALSE(on_discardable_called);

  detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(heads_children_manager.IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(SynchronyAndConcurrencyHeadsChildrenManagerTest, Attach) {
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  std::map<storage::CommitId, std::set<storage::CommitId>> graph{
      {one, {convert::ToString(storage::kFirstPageCommitId)}},
      {two, {convert::ToString(storage::kFirstPageCommitId)}},
      {three, {convert::ToString(storage::kFirstPageCommitId)}}};
  std::vector<storage::CommitId> heads{};
  heads.reserve(graph.size());
  for (const auto& [head, _] : graph) {
    heads.push_back(head);
  }
  size_t concurrency = std::get<0>(GetParam());
  std::vector<storage::CommitId> attachment_choices{};
  for (size_t index{0}; index < concurrency; index++) {
    attachment_choices.push_back(heads[index % heads.size()]);
  }
  inspect_deprecated::Node heads_node =
      inspect_deprecated::Node(convert::ToString(kHeadsInspectPathComponent));
  std::unique_ptr<HeadCommitsSubstitutePageStorage> page_storage =
      std::make_unique<HeadCommitsSubstitutePageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager),
                                             std::get<1>(GetParam()), test_loop().dispatcher()};
  size_t callbacks_called{0};
  std::vector<fit::closure> detachers{concurrency};
  bool on_discardable_called;

  HeadsChildrenManager heads_children_manager{dispatcher(), &heads_node, &inspectable_page};
  heads_children_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&heads_children_manager)
        ->Attach(CommitIdToDisplayName(attachment_choices[index]),
                 callback::Capture([&] { callbacks_called++; }, &detachers[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(detachers, Each(IsTrue()));

  for (const auto& detacher : detachers) {
    EXPECT_FALSE(on_discardable_called);
    detacher();
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(heads_children_manager.IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(SynchronyAndConcurrencyHeadsChildrenManagerTest, GetNamesErrorGettingActivePageManager) {
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  std::map<storage::CommitId, std::set<storage::CommitId>> graph{
      {one, {convert::ToString(storage::kFirstPageCommitId)}},
      {two, {convert::ToString(storage::kFirstPageCommitId)}},
      {three, {convert::ToString(storage::kFirstPageCommitId)}}};
  size_t concurrency = std::get<0>(GetParam());
  inspect_deprecated::Node heads_node =
      inspect_deprecated::Node(convert::ToString(kHeadsInspectPathComponent));
  SubstituteInspectablePage inspectable_page{nullptr, std::get<1>(GetParam()),
                                             test_loop().dispatcher()};
  size_t callbacks_called = 0;
  std::vector<std::set<std::string>> nameses(concurrency);
  size_t on_discardable_calls = 0;

  HeadsChildrenManager heads_children_manager{dispatcher(), &heads_node, &inspectable_page};
  heads_children_manager.SetOnDiscardable([&on_discardable_calls] { on_discardable_calls++; });
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&heads_children_manager)
        ->GetNames(callback::Capture([&] { callbacks_called++; }, &nameses[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(nameses, Each(IsEmpty()));
  switch (std::get<1>(GetParam())) {
    case Synchrony::ASYNCHRONOUS:
      EXPECT_EQ(on_discardable_calls, 1u);
      break;
    case Synchrony::SYNCHRONOUS:
      // We may have made the calls concurrently (all before a call to |RunLoopUntilIdle|), but if
      // the |NewInspection| method of the |InspectablePage| used by the |HeadsChildrenManager|
      // under test executes its calls synchronously, the |HeadsChildrenManager| under test will
      // dither between emptiness and nonemptiness.
      EXPECT_THAT(on_discardable_calls, Ge(1u));
      EXPECT_THAT(on_discardable_calls, Le(concurrency));
      break;
  }
}

TEST_P(SynchronyAndConcurrencyHeadsChildrenManagerTest, GetNamesErrorGettingCommits) {
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
  storage::CommitId two =
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize);
  storage::CommitId three =
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize);
  std::map<storage::CommitId, std::set<storage::CommitId>> graph{
      {one, {convert::ToString(storage::kFirstPageCommitId)}},
      {two, {convert::ToString(storage::kFirstPageCommitId)}},
      {three, {convert::ToString(storage::kFirstPageCommitId)}}};
  size_t concurrency = std::get<0>(GetParam());
  inspect_deprecated::Node heads_node =
      inspect_deprecated::Node(convert::ToString(kHeadsInspectPathComponent));
  std::unique_ptr<HeadCommitsSubstitutePageStorage> page_storage =
      std::make_unique<HeadCommitsSubstitutePageStorage>(graph);
  page_storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager),
                                             std::get<1>(GetParam()), test_loop().dispatcher()};
  size_t callbacks_called = 0;
  std::vector<std::set<std::string>> nameses(concurrency);
  size_t on_discardable_calls = 0;

  HeadsChildrenManager heads_children_manager{dispatcher(), &heads_node, &inspectable_page};
  heads_children_manager.SetOnDiscardable([&on_discardable_calls] { on_discardable_calls++; });
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&heads_children_manager)
        ->GetNames(callback::Capture([&] { callbacks_called++; }, &nameses[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(nameses, Each(IsEmpty()));
  switch (std::get<1>(GetParam())) {
    case Synchrony::ASYNCHRONOUS:
      EXPECT_EQ(on_discardable_calls, 1u);
      break;
    case Synchrony::SYNCHRONOUS:
      // We may have made the calls concurrently (all before a call to |RunLoopUntilIdle|), but if
      // the |NewInspection| method of the |InspectablePage| used by the |HeadsChildrenManager|
      // under test executes its calls synchronously, the |HeadsChildrenManager| under test will
      // dither between emptiness and nonemptiness.
      EXPECT_THAT(on_discardable_calls, Ge(1u));
      EXPECT_THAT(on_discardable_calls, Le(concurrency));
      break;
  }
}

TEST_F(HeadsChildrenManagerTest, AttachInvalidName) {
  inspect_deprecated::Node heads_node =
      inspect_deprecated::Node(convert::ToString(kHeadsInspectPathComponent));
  DummyInspectablePage inspectable_page{};
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  HeadsChildrenManager heads_children_manager{dispatcher(), &heads_node, &inspectable_page};
  heads_children_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  static_cast<inspect_deprecated::ChildrenManager*>(&heads_children_manager)
      ->Attach("Definitely not the display string of a commit ID",
               callback::Capture(callback::SetWhenCalled(&callback_called), &detacher));
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  // The HeadsChildrenManager under test did not surrender program control during the call to
  // Attach so it never needed to check its emptiness after regaining program control.
  EXPECT_FALSE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  heads_children_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

INSTANTIATE_TEST_SUITE_P(HeadsChildrenManagerTest, SynchronyHeadsChildrenManagerTest,
                         Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS));

INSTANTIATE_TEST_SUITE_P(HeadsChildrenManagerTest, SynchronyAndConcurrencyHeadsChildrenManagerTest,
                         Combine(Range<size_t>(kMinimumConcurrency, kMaximumConcurrency + 1),
                                 Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS)));

}  // namespace
}  // namespace ledger
