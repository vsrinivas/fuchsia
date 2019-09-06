// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/active_page_manager.h"

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
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
#include "src/lib/fxl/macros.h"

namespace ledger {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointee;
using ::testing::ResultOf;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAreArray;

std::unique_ptr<MergeResolver> GetDummyResolver(Environment* environment,
                                                storage::PageStorage* storage) {
  return std::make_unique<MergeResolver>(
      [] {}, environment, storage,
      std::make_unique<backoff::ExponentialBackoff>(
          zx::sec(0), 1u, zx::sec(0), environment->random()->NewBitGenerator<uint64_t>()));
}

class IdsAndParentIdsPageStorage final : public storage::PageStorageEmptyImpl {
 public:
  explicit IdsAndParentIdsPageStorage(
      std::map<storage::CommitId, std::set<storage::CommitId>> graph)
      : graph_(std::move(graph)), fail_(-1) {
    for (const auto& [child, parents] : graph_) {
      heads_.insert(child);
      for (const storage::CommitId& parent : parents) {
        heads_.erase(parent);
      }
    }
  }
  ~IdsAndParentIdsPageStorage() override = default;

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
    if (fail_ == 0) {
      callback(storage::Status::INTERNAL_ERROR, nullptr);
      return;
    }
    if (fail_ > 0) {
      fail_--;
    }

    if (const auto& it = graph_.find(commit_id.ToString()); it == graph_.end()) {
      callback(storage::Status::INTERNAL_NOT_FOUND, nullptr);
      return;
    }

    callback(storage::Status::OK, std::make_unique<storage::IdAndParentIdsCommit>(
                                      commit_id.ToString(), graph_[commit_id.ToString()]));
  }
  void AddCommitWatcher(storage::CommitWatcher* watcher) override {}
  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override {}

  std::set<storage::CommitId> heads_;
  std::map<storage::CommitId, std::set<storage::CommitId>> graph_;

  // The number of calls to complete successfully before terminating calls unsuccessfully. -1 to
  // always complete calls successfully.
  int64_t fail_;
};

class FakePageSync : public sync_coordinator::PageSyncEmptyImpl {
 public:
  void Start() override { start_called = true; }

  void SetOnBacklogDownloaded(fit::closure on_backlog_downloaded_callback) override {
    this->on_backlog_downloaded_callback = std::move(on_backlog_downloaded_callback);
  }

  void SetOnIdle(fit::closure on_idle) override { this->on_idle = std::move(on_idle); }

  void SetSyncWatcher(sync_coordinator::SyncStateWatcher* watcher) override {
    this->watcher = watcher;
  }

  bool start_called = false;
  sync_coordinator::SyncStateWatcher* watcher = nullptr;
  fit::closure on_backlog_downloaded_callback;
  fit::closure on_idle;
};

class ActivePageManagerTest : public TestWithEnvironment {
 public:
  ActivePageManagerTest() {}
  ~ActivePageManagerTest() override = default;

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id_ = storage::PageId(::fuchsia::ledger::PAGE_ID_SIZE, 'a');
  }

  void DrainLoop() { RunLoopRepeatedlyFor(storage::fake::kFakePageStorageDelay); }

  std::unique_ptr<storage::PageStorage> MakeStorage() {
    return std::make_unique<storage::fake::FakePageStorage>(&environment_, page_id_);
  }

  storage::PageId page_id_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ActivePageManagerTest);
};

TEST_F(ActivePageManagerTest, OnEmptyCallback) {
  bool on_empty_called = false;
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  DrainLoop();
  EXPECT_FALSE(on_empty_called);

  bool called;
  Status status;
  PagePtr page1;
  PagePtr page2;

  auto page_impl1 = std::make_unique<PageImpl>(page_id_, page1.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl1),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);

  auto page_impl2 = std::make_unique<PageImpl>(page_id_, page2.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl2),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);

  page1.Unbind();
  page2.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_empty_called);
  EXPECT_TRUE(active_page_manager.IsEmpty());

  on_empty_called = false;
  PagePtr page3;
  auto page_impl3 = std::make_unique<PageImpl>(page_id_, page3.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl3),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_FALSE(active_page_manager.IsEmpty());

  page3.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_empty_called);
  EXPECT_TRUE(active_page_manager.IsEmpty());

  on_empty_called = false;
  PageSnapshotPtr snapshot;
  active_page_manager.BindPageSnapshot(std::make_unique<const storage::CommitEmptyImpl>(),
                                       snapshot.NewRequest(), "");
  DrainLoop();
  EXPECT_FALSE(active_page_manager.IsEmpty());
  snapshot.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(ActivePageManagerTest, DeletingPageManagerClosesConnections) {
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  auto active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);

  bool called;
  Status status;
  PagePtr page;
  auto page_impl = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  active_page_manager->AddPageImpl(std::move(page_impl),
                                   callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  bool page_closed;
  page.set_error_handler(
      [callback = callback::SetWhenCalled(&page_closed)](zx_status_t status) { callback(); });

  active_page_manager.reset();
  DrainLoop();
  EXPECT_TRUE(page_closed);
}

TEST_F(ActivePageManagerTest, OnEmptyCallbackWithWatcher) {
  bool on_empty_called = false;
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  DrainLoop();
  // PageManager is empty, but on_empty should not have be called, yet.
  EXPECT_FALSE(on_empty_called);
  EXPECT_TRUE(active_page_manager.IsEmpty());

  bool called;
  Status internal_status;
  PagePtr page1;
  PagePtr page2;
  auto page_impl1 = std::make_unique<PageImpl>(page_id_, page1.NewRequest());
  active_page_manager.AddPageImpl(
      std::move(page_impl1), callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(internal_status, Status::OK);

  auto page_impl2 = std::make_unique<PageImpl>(page_id_, page2.NewRequest());
  active_page_manager.AddPageImpl(
      std::move(page_impl2), callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(internal_status, Status::OK);

  page1->Put(convert::ToArray("key1"), convert::ToArray("value1"));

  PageWatcherPtr watcher;
  fidl::InterfaceRequest<PageWatcher> watcher_request = watcher.NewRequest();
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher));

  page1.Unbind();
  page2.Unbind();
  snapshot.Unbind();
  DrainLoop();
  EXPECT_FALSE(active_page_manager.IsEmpty());
  EXPECT_FALSE(on_empty_called);

  watcher_request.TakeChannel();
  DrainLoop();
  EXPECT_TRUE(active_page_manager.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(ActivePageManagerTest, DelayBindingUntilSyncBacklogDownloaded) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(fake_page_sync_ptr->watcher, nullptr);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  ActivePageManager active_page_manager(&environment_, std::move(storage),
                                        std::move(fake_page_sync), std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_TRUE(fake_page_sync_ptr->start_called);
  EXPECT_TRUE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  bool called;
  Status internal_status;
  PagePtr page;
  auto page_impl1 = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  active_page_manager.AddPageImpl(
      std::move(page_impl1), callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  // The page should be bound, but except from GetId, no other method should
  // be executed, until the sync backlog is downloaded.
  DrainLoop();
  EXPECT_FALSE(called);

  PageId found_page_id;
  page->GetId(callback::Capture(callback::SetWhenCalled(&called), &found_page_id));
  DrainLoop();
  EXPECT_TRUE(called);
  PageId expected_page_id;
  convert::ToArray(page_id_, &expected_page_id.id);
  EXPECT_EQ(found_page_id.id, expected_page_id.id);

  // Clear should not be executed.
  page->Clear();

  fake_page_sync_ptr->on_backlog_downloaded_callback();
  // BindPage callback can now be executed; Clear callback should then be
  // called.
  DrainLoop();
  EXPECT_TRUE(called);

  // Check that a second call on the same manager is not delayed.
  page.Unbind();
  auto page_impl2 = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  active_page_manager.AddPageImpl(
      std::move(page_impl2), callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(internal_status, Status::OK);

  page->GetId(callback::Capture(callback::SetWhenCalled(&called), &std::ignore));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(ActivePageManagerTest, DelayBindingUntilSyncTimeout) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(fake_page_sync_ptr->watcher, nullptr);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  ActivePageManager active_page_manager(
      &environment_, std::move(storage), std::move(fake_page_sync), std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC, zx::sec(0));

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_TRUE(fake_page_sync_ptr->start_called);
  EXPECT_TRUE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  bool called;
  Status status;
  PagePtr page;
  auto page_impl = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);

  page->GetId(callback::Capture(callback::SetWhenCalled(&called), &std::ignore));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(ActivePageManagerTest, ExitWhenSyncFinishes) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(fake_page_sync_ptr->watcher, nullptr);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  ActivePageManager active_page_manager(
      &environment_, std::move(storage), std::move(fake_page_sync), std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC, zx::sec(0));

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);

  bool called;
  active_page_manager.set_on_empty(callback::SetWhenCalled(&called));

  async::PostTask(dispatcher(), [fake_page_sync_ptr] { fake_page_sync_ptr->on_idle(); });

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_TRUE(active_page_manager.IsEmpty());
}

TEST_F(ActivePageManagerTest, DontDelayBindingWithLocalPageStorage) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(fake_page_sync_ptr->watcher, nullptr);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  ActivePageManager active_page_manager(&environment_, std::move(storage),
                                        std::move(fake_page_sync), std::move(merger),
                                        ActivePageManager::PageStorageState::AVAILABLE,
                                        // Use a long timeout to ensure the test does not hit it.
                                        zx::sec(3600));

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_TRUE(fake_page_sync_ptr->start_called);
  EXPECT_TRUE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  bool called;
  Status status;
  PagePtr page;
  auto page_impl = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  // The page should be bound immediately.
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);

  page->GetId(callback::Capture(callback::SetWhenCalled(&called), &std::ignore));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(ActivePageManagerTest, GetCommitsSuccessGraphFullyPresent) {
  storage::CommitId zero = storage::kFirstPageCommitId.ToString();
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
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

  //    0
  //   / \
  //  1   3
  //  |   |
  //  2   4
  //   \ /
  //    5
  //    |
  //    6
  //   / \
  //  7   8
  //  |
  //  9
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {zero, {}},          {one, {zero}}, {two, {one}},   {three, {zero}}, {four, {three}},
      {five, {two, four}}, {six, {five}}, {seven, {six}}, {eight, {six}},  {nine, {seven}},
  };

  bool callback_called;
  Status status;
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  bool on_empty_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));

  active_page_manager.GetCommits(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commits));

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Eq(Status::OK));
  EXPECT_THAT(commits, SizeIs(graph.size()));
  for (const auto& [commit_id, parents] : graph) {
    EXPECT_THAT(commits, Contains(Pointee(storage::MatchesCommit(commit_id, parents))));
  }
  EXPECT_TRUE(on_empty_called);
}

TEST_F(ActivePageManagerTest, GetCommitsSuccessGraphPartiallyPresent) {
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

  bool callback_called;
  Status status;
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  bool on_empty_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));

  active_page_manager.GetCommits(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commits));

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Eq(Status::OK));
  EXPECT_THAT(commits, SizeIs(graph.size()));
  for (const auto& [commit_id, parents] : graph) {
    EXPECT_THAT(commits, Contains(Pointee(storage::MatchesCommit(commit_id, parents))));
  }
  EXPECT_TRUE(on_empty_called);
}

TEST_F(ActivePageManagerTest, GetCommitsInternalError) {
  storage::CommitId zero = storage::kFirstPageCommitId.ToString();
  storage::CommitId one =
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize);
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

  // Nine storage operations are required to traverse this graph (one GetHeads call and eight
  // GetCommit calls).
  //
  //    0
  //   / \
  //  1   3
  //  |   |
  //  2   4
  //   \ /
  //    5
  //    |
  //    6
  //   / \
  //  7   8
  //  |
  //  9
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {zero, {}},          {one, {zero}}, {two, {one}},   {three, {zero}}, {four, {three}},
      {five, {two, four}}, {six, {five}}, {seven, {six}}, {eight, {six}},  {nine, {seven}},
  };
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  const size_t successful_storage_call_count = std::uniform_int_distribution(0u, 8u)(bit_generator);

  bool callback_called;
  Status status;
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  bool on_empty_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  storage->fail_after_successful_calls(successful_storage_call_count);
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));

  active_page_manager.GetCommits(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commits));

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Ne(Status::OK));
  // We don't assert anything about the contents of |commits|. Maybe it contains all results before
  // the failure occurred? Maybe a portion of those results? Maybe it's empty? No state of |commits|
  // is guaranteed (except the bare minimum: that it is safe to destroy).
  // If |successful_storage_call_count| was zero, |active_page_manager|'s call to its page storage's
  // GetHeads method failed, |active_page_manager| never became non-empty (or surrendered program
  // control), and |active_page_manager| thus never needed to check its emptiness.
  EXPECT_THAT(on_empty_called, Eq(bool(successful_storage_call_count)));
}

TEST_F(ActivePageManagerTest, GetCommitSuccess) {
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {storage::kFirstPageCommitId.ToString(), {}}};

  bool callback_called;
  Status status;
  std::unique_ptr<const storage::Commit> commit;
  bool on_empty_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));

  active_page_manager.GetCommit(
      storage::kFirstPageCommitId.ToString(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commit));

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Eq(Status::OK));
  EXPECT_THAT(commit, Pointee(storage::MatchesCommit(storage::kFirstPageCommitId.ToString(), {})));
  EXPECT_TRUE(on_empty_called);
}

TEST_F(ActivePageManagerTest, GetCommitInternalError) {
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {storage::kFirstPageCommitId.ToString(), {}}};

  bool callback_called;
  Status status;
  std::unique_ptr<const storage::Commit> commit;
  bool on_empty_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  storage->fail_after_successful_calls(0);
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));

  active_page_manager.GetCommit(
      storage::kFirstPageCommitId.ToString(),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commit));

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Ne(Status::OK));
  // We don't assert anything about |commit| (except the bare minimum: that it is safe to destroy).
  EXPECT_TRUE(on_empty_called);
}

}  // namespace
}  // namespace ledger
