// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/active_page_manager.h"

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
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/ledger/lib/vmo/vector.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {
namespace {

using ::testing::Combine;
using ::testing::Contains;
using ::testing::Each;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ne;
using ::testing::Pointee;
using ::testing::Range;
using ::testing::SizeIs;
using ::testing::Values;
using ::testing::WithParamInterface;

// Used by this test and associated test substitutes to control whether or not to task-hop at
// various opportunities throughout the test.
enum class Synchrony {
  ASYNCHRONOUS = 0,
  SYNCHRONOUS = 1,
};

storage::Entry CreateStorageEntry(const std::string& key, uint32_t index) {
  return storage::Entry{key, storage::ObjectIdentifier{index, storage::ObjectDigest(""), nullptr},
                        storage::KeyPriority::EAGER, "This string is not a real storage::EntryId."};
}

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
class IdsAndParentIdsPageStorage final : public storage::PageStorageEmptyImpl {
 public:
  explicit IdsAndParentIdsPageStorage(
      std::map<storage::CommitId, std::set<storage::CommitId>> graph,
      Synchrony get_commit_synchrony, async_dispatcher_t* dispatcher)
      : graph_(std::move(graph)),
        dispatcher_(dispatcher),
        get_commit_synchrony_(get_commit_synchrony),
        fail_(-1) {
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
    auto implementation = [&, commit_id = convert::ToString(commit_id),
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
    switch (get_commit_synchrony_) {
      case Synchrony::ASYNCHRONOUS:
        async::PostTask(dispatcher_, std::move(implementation));
        break;
      case Synchrony::SYNCHRONOUS:
        implementation();
        break;
    }
  }
  void AddCommitWatcher(storage::CommitWatcher* watcher) override {}
  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override {}

  std::set<storage::CommitId> heads_;
  std::map<storage::CommitId, std::set<storage::CommitId>> graph_;
  async_dispatcher_t* dispatcher_;
  Synchrony get_commit_synchrony_;

  // The number of calls to complete successfully before terminating calls unsuccessfully. -1 to
  // always complete calls successfully.
  int64_t fail_;
};

// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=36298): Deduplicate and canonicalize
// this test substitute.
class EntriesPageStorage final : public storage::PageStorageEmptyImpl {
 public:
  explicit EntriesPageStorage(const std::map<std::string, std::vector<uint8_t>>& entries,
                              Synchrony get_object_part_synchrony,
                              Synchrony get_commit_contents_first_synchrony,
                              Synchrony get_commit_contents_second_synchrony,
                              Synchrony get_entry_from_commit_synchrony,
                              async_dispatcher_t* dispatcher)
      : get_object_part_synchrony_(get_object_part_synchrony),
        get_commit_contents_first_synchrony_(get_commit_contents_first_synchrony),
        get_commit_contents_second_synchrony_(get_commit_contents_second_synchrony),
        get_entry_from_commit_synchrony_(get_entry_from_commit_synchrony),
        dispatcher_(dispatcher),
        fail_(-1) {
    for (const auto& [key, value] : entries) {
      uint32_t index = entries_.size();
      entries_.try_emplace(key, value, index);
      keys_by_index_.emplace(index, key);
    }
  }
  ~EntriesPageStorage() override = default;

  void fail_after_successful_calls(int64_t successful_call_count) { fail_ = successful_call_count; }

 private:
  // storage::PageStorageEmptyImpl:
  void AddCommitWatcher(storage::CommitWatcher* watcher) override {}
  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override {}
  void GetObjectPart(storage::ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
                     storage::PageStorage::Location location,
                     fit::function<void(storage::Status, ledger::SizedVmo)> callback) override {
    if (offset != 0) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }
    if (max_size != 1024) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }
    if (location != storage::PageStorage::Location::Local()) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }

    auto implementation = [this, index = object_identifier.key_index(),
                           callback = std::move(callback)] {
      if (fail_ == 0) {
        callback(storage::Status::INTERNAL_ERROR, {});
        return;
      }
      if (fail_ > 0) {
        fail_--;
      }
      auto index_it = keys_by_index_.find(index);
      if (index_it == keys_by_index_.end()) {
        callback(storage::Status::INTERNAL_NOT_FOUND, {});
        return;
      }
      auto value_it = entries_.find(index_it->second);
      if (value_it == entries_.end()) {
        callback(storage::Status::INTERNAL_NOT_FOUND, {});
        return;
      }
      ledger::SizedVmo sized_vmo;
      ASSERT_TRUE(ledger::VmoFromVector(value_it->second.first, &sized_vmo));
      callback(storage::Status::OK, std::move(sized_vmo));
    };
    switch (get_object_part_synchrony_) {
      case Synchrony::ASYNCHRONOUS:
        async::PostTask(dispatcher_, std::move(implementation));
        break;
      case Synchrony::SYNCHRONOUS:
        implementation();
        break;
    }
  }
  void GetCommitContents(const storage::Commit& commit, std::string min_key,
                         fit::function<bool(storage::Entry)> on_next,
                         fit::function<void(storage::Status)> on_done) override {
    if (!min_key.empty()) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }
    auto implementation = [this, on_next = std::move(on_next),
                           on_done = std::move(on_done)]() mutable {
      if (fail_ == 0) {
        on_done(storage::Status::INTERNAL_ERROR);
        return;
      }
      if (fail_ > 0) {
        fail_--;
      }
      // TODO(nathaniel): Parameterizedly delay to a later task (or not) between individual on-next
      // calls.
      for (const auto& [key, value_and_index] : entries_) {
        if (!on_next(CreateStorageEntry(key, value_and_index.second))) {
          LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
        }
      }
      switch (get_commit_contents_second_synchrony_) {
        case Synchrony::ASYNCHRONOUS:
          async::PostTask(dispatcher_,
                          [on_done = std::move(on_done)] { on_done(storage::Status::OK); });
          break;
        case Synchrony::SYNCHRONOUS:
          on_done(storage::Status::OK);
          break;
      }
    };
    switch (get_commit_contents_first_synchrony_) {
      case Synchrony::ASYNCHRONOUS:
        async::PostTask(dispatcher_, std::move(implementation));
        break;
      case Synchrony::SYNCHRONOUS:
        implementation();
        break;
    }
  }
  void GetEntryFromCommit(const storage::Commit& commit, std::string key,
                          fit::function<void(storage::Status, storage::Entry)> on_done) override {
    auto implementation = [this, key = std::move(key), on_done = std::move(on_done)] {
      if (fail_ == 0) {
        on_done(storage::Status::INTERNAL_ERROR, {});
        return;
      }
      if (fail_ > 0) {
        fail_--;
      }
      auto it = entries_.find(key);
      if (it == entries_.end()) {
        on_done(storage::Status::KEY_NOT_FOUND, {});
        return;
      }
      on_done(storage::Status::OK, CreateStorageEntry(key, it->second.second));
    };
    switch (get_entry_from_commit_synchrony_) {
      case Synchrony::ASYNCHRONOUS:
        async::PostTask(dispatcher_, std::move(implementation));
        break;
      case Synchrony::SYNCHRONOUS:
        implementation();
        break;
    }
  }

  std::map<std::string, std::pair<std::vector<uint8_t>, uint32_t>> entries_;
  std::map<uint32_t, std::string> keys_by_index_;
  Synchrony get_object_part_synchrony_;
  Synchrony get_commit_contents_first_synchrony_;
  Synchrony get_commit_contents_second_synchrony_;
  Synchrony get_entry_from_commit_synchrony_;
  async_dispatcher_t* dispatcher_;

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

  void SetOnPaused(fit::closure on_paused) override { this->on_paused = std::move(on_paused); }

  void SetSyncWatcher(sync_coordinator::SyncStateWatcher* watcher) override {
    this->watcher = watcher;
  }

  bool start_called = false;
  sync_coordinator::SyncStateWatcher* watcher = nullptr;
  fit::closure on_backlog_downloaded_callback;
  fit::closure on_paused;
};

class ActivePageManagerTest : public TestWithEnvironment {
 public:
  ActivePageManagerTest() = default;
  ActivePageManagerTest(const ActivePageManagerTest&) = delete;
  ActivePageManagerTest& operator=(const ActivePageManagerTest&) = delete;
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
};

TEST_F(ActivePageManagerTest, OnDiscardableCallback) {
  bool on_discardable_called = false;
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  DrainLoop();
  EXPECT_FALSE(on_discardable_called);

  bool called;
  Status status;
  PagePtr page1;
  PagePtr page2;

  auto page_impl1 =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page1.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl1),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);

  auto page_impl2 =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page2.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl2),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);

  page1.Unbind();
  page2.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_discardable_called);
  EXPECT_TRUE(active_page_manager.IsDiscardable());

  on_discardable_called = false;
  PagePtr page3;
  auto page_impl3 =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page3.NewRequest());
  active_page_manager.AddPageImpl(std::move(page_impl3),
                                  callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_FALSE(active_page_manager.IsDiscardable());

  page3.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_discardable_called);
  EXPECT_TRUE(active_page_manager.IsDiscardable());

  on_discardable_called = false;
  PageSnapshotPtr snapshot;
  active_page_manager.BindPageSnapshot(std::make_unique<const storage::CommitEmptyImpl>(),
                                       snapshot.NewRequest(), "");
  DrainLoop();
  EXPECT_FALSE(active_page_manager.IsDiscardable());
  snapshot.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_discardable_called);
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
  auto page_impl =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page.NewRequest());
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

TEST_F(ActivePageManagerTest, OnDiscardableCallbackWithWatcher) {
  bool on_discardable_called = false;
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  DrainLoop();
  // PageManager is discardable, but the callback should not have be called, yet.
  EXPECT_FALSE(on_discardable_called);
  EXPECT_TRUE(active_page_manager.IsDiscardable());

  bool called;
  Status internal_status;
  PagePtr page1;
  PagePtr page2;
  auto page_impl1 =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page1.NewRequest());
  active_page_manager.AddPageImpl(
      std::move(page_impl1), callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(internal_status, Status::OK);

  auto page_impl2 =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page2.NewRequest());
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
  EXPECT_FALSE(active_page_manager.IsDiscardable());
  EXPECT_FALSE(on_discardable_called);

  watcher_request.TakeChannel();
  DrainLoop();
  EXPECT_TRUE(active_page_manager.IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
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
  auto page_impl1 =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page.NewRequest());
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
  auto page_impl2 =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page.NewRequest());
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
  auto page_impl =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page.NewRequest());
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
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&called));

  async::PostTask(dispatcher(), [fake_page_sync_ptr] { fake_page_sync_ptr->on_paused(); });

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_TRUE(active_page_manager.IsDiscardable());
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
  auto page_impl =
      std::make_unique<PageImpl>(environment_.dispatcher(), page_id_, page.NewRequest());
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

class IdsAndParentIdsPageStorageActivePageManagerTest : public ActivePageManagerTest,
                                                        public WithParamInterface<Synchrony> {};
class IdsAndParentIdsPageStoragePlusFailureIntegerActivePageManagerTest
    : public ActivePageManagerTest,
      public WithParamInterface<std::tuple<Synchrony, size_t>> {};
class EntriesPageStorageActivePageManagerTest
    : public ActivePageManagerTest,
      public WithParamInterface<std::tuple<Synchrony, Synchrony, Synchrony, Synchrony>> {};

TEST_P(IdsAndParentIdsPageStorageActivePageManagerTest, GetCommitsSuccessGraphFullyPresent) {
  storage::CommitId zero = convert::ToString(storage::kFirstPageCommitId);
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
  bool on_discardable_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph, GetParam(), environment_.dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetCommits(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commits));
  RunLoopUntilIdle();

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Eq(Status::OK));
  EXPECT_THAT(commits, SizeIs(graph.size()));
  for (const auto& [commit_id, parents] : graph) {
    EXPECT_THAT(commits, Contains(Pointee(storage::MatchesCommit(commit_id, parents))));
  }
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(IdsAndParentIdsPageStorageActivePageManagerTest, GetCommitsSuccessGraphPartiallyPresent) {
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
  bool on_discardable_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph, GetParam(), environment_.dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetCommits(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commits));
  RunLoopUntilIdle();

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Eq(Status::OK));
  EXPECT_THAT(commits, SizeIs(graph.size()));
  for (const auto& [commit_id, parents] : graph) {
    EXPECT_THAT(commits, Contains(Pointee(storage::MatchesCommit(commit_id, parents))));
  }
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(IdsAndParentIdsPageStoragePlusFailureIntegerActivePageManagerTest, GetCommitsInternalError) {
  storage::CommitId zero = convert::ToString(storage::kFirstPageCommitId);
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
  const size_t successful_storage_call_count = std::get<1>(GetParam());

  bool callback_called;
  Status status;
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  bool on_discardable_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph, std::get<0>(GetParam()),
                                                   environment_.dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  storage->fail_after_successful_calls(successful_storage_call_count);
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetCommits(
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commits));
  RunLoopUntilIdle();

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Ne(Status::OK));
  // We don't assert anything about the contents of |commits|. Maybe it contains all results before
  // the failure occurred? Maybe a portion of those results? Maybe it's empty? No state of |commits|
  // is guaranteed (except the bare minimum: that it is safe to destroy).
  // If |successful_storage_call_count| was zero, |active_page_manager|'s call to its page storage's
  // GetHeads method failed, |active_page_manager| never became non-empty (or surrendered program
  // control), and |active_page_manager| thus never needed to check its emptiness.
  EXPECT_THAT(on_discardable_called, Eq(bool(successful_storage_call_count)));
}

TEST_P(IdsAndParentIdsPageStorageActivePageManagerTest, GetCommitSuccess) {
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {convert::ToString(storage::kFirstPageCommitId), {}}};

  bool callback_called;
  Status status;
  std::unique_ptr<const storage::Commit> commit;
  bool on_discardable_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph, GetParam(), environment_.dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetCommit(
      convert::ToString(storage::kFirstPageCommitId),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commit));
  RunLoopUntilIdle();

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Eq(Status::OK));
  EXPECT_THAT(commit,
              Pointee(storage::MatchesCommit(convert::ToString(storage::kFirstPageCommitId), {})));
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(IdsAndParentIdsPageStorageActivePageManagerTest, GetCommitInternalError) {
  std::map<storage::CommitId, std::set<storage::CommitId>> graph = {
      {convert::ToString(storage::kFirstPageCommitId), {}}};

  bool callback_called;
  Status status;
  std::unique_ptr<const storage::Commit> commit;
  bool on_discardable_called = false;
  std::unique_ptr<IdsAndParentIdsPageStorage> storage =
      std::make_unique<IdsAndParentIdsPageStorage>(graph, GetParam(), environment_.dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  storage->fail_after_successful_calls(0);
  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetCommit(
      convert::ToString(storage::kFirstPageCommitId),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &commit));
  RunLoopUntilIdle();

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Ne(Status::OK));
  // We don't assert anything about |commit| (except the bare minimum: that it is safe to destroy).
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(EntriesPageStorageActivePageManagerTest, GetEntriesSuccess) {
  std::map<std::string, std::vector<uint8_t>> entries = {
      {"one", {1}},  {"two", {2}}, {"three", {3}}, {"four", {4}},
      {"five", {5}}, {"six", {6}}, {"seven", {7}}};

  bool callback_called;
  Status status;
  std::vector<storage::Entry> storage_entries{};
  bool on_discardable_called = false;
  std::unique_ptr<EntriesPageStorage> storage = std::make_unique<EntriesPageStorage>(
      entries, std::get<0>(GetParam()), std::get<1>(GetParam()), std::get<2>(GetParam()),
      std::get<3>(GetParam()), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetEntries(
      storage::IdAndParentIdsCommit{convert::ToString(storage::kFirstPageCommitId), {}}, "",
      [&](const storage::Entry& storage_entry) {
        storage_entries.push_back(storage_entry);
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Eq(Status::OK));
  EXPECT_THAT(storage_entries, SizeIs(entries.size()));
  for (const auto& [key, unused_value] : entries) {
    EXPECT_THAT(storage_entries, Contains(Field("key", &storage::Entry::key, key)));
  }
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(EntriesPageStorageActivePageManagerTest, GetEntriesInternalError) {
  bool callback_called;
  Status status;
  std::vector<storage::Entry> storage_entries{};
  bool on_discardable_called = false;
  std::unique_ptr<EntriesPageStorage> storage = std::make_unique<EntriesPageStorage>(
      std::map<std::string, std::vector<uint8_t>>{}, std::get<0>(GetParam()),
      std::get<1>(GetParam()), std::get<2>(GetParam()), std::get<3>(GetParam()),
      test_loop().dispatcher());
  storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetEntries(
      storage::IdAndParentIdsCommit(convert::ToString(storage::kFirstPageCommitId), {}), "",
      [&](const storage::Entry& storage_entry) {
        storage_entries.push_back(storage_entry);
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Ne(Status::OK));
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(EntriesPageStorageActivePageManagerTest, GetValueSuccess) {
  std::map<std::string, std::vector<uint8_t>> entries = {{"zero", {}},
                                                         {"one", {1}},
                                                         {"two", {2, 2}},
                                                         {"three", {3, 3, 3}},
                                                         {"four", {4, 4, 4, 4}},
                                                         {"five", {5, 5, 5, 5, 5}},
                                                         {"six", {6, 6, 6, 6, 6, 6}},
                                                         {"seven", {7, 7, 7, 7, 7, 7, 7}}};

  size_t callbacks_called{0};
  std::vector<Status> statuses{};
  std::map<std::string, std::vector<uint8_t>> emitted_entries{};
  bool on_discardable_called = false;
  std::unique_ptr<EntriesPageStorage> storage = std::make_unique<EntriesPageStorage>(
      entries, std::get<0>(GetParam()), std::get<1>(GetParam()), std::get<2>(GetParam()),
      std::get<3>(GetParam()), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  for (const auto& [key, _] : entries) {
    active_page_manager.GetValue(
        storage::IdAndParentIdsCommit{convert::ToString(storage::kFirstPageCommitId), {}}, key,
        [&, key = key](Status status, const std::vector<uint8_t>& value) {
          callbacks_called++;
          statuses.push_back(status);
          emitted_entries[key] = value;
        });
  }
  RunLoopUntilIdle();

  ASSERT_EQ(callbacks_called, entries.size());
  EXPECT_THAT(statuses, Each(Eq(Status::OK)));
  EXPECT_EQ(emitted_entries, entries);
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(EntriesPageStorageActivePageManagerTest, GetValueGetEntryError) {
  bool callback_called;
  Status status;
  std::vector<uint8_t> value;
  bool on_discardable_called = false;
  std::unique_ptr<EntriesPageStorage> storage = std::make_unique<EntriesPageStorage>(
      std::map<std::string, std::vector<uint8_t>>{}, std::get<0>(GetParam()),
      std::get<1>(GetParam()), std::get<2>(GetParam()), std::get<3>(GetParam()),
      test_loop().dispatcher());
  storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetValue(
      storage::IdAndParentIdsCommit{convert::ToString(storage::kFirstPageCommitId), {}},
      "my happy fun key",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &value));
  RunLoopUntilIdle();

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Ne(Status::OK));
  EXPECT_TRUE(on_discardable_called);
}

TEST_P(EntriesPageStorageActivePageManagerTest, GetValueGetObjectPartError) {
  std::string key = "your happy fun key";
  bool callback_called;
  Status status;
  std::vector<uint8_t> value;
  bool on_discardable_called = false;
  std::unique_ptr<EntriesPageStorage> storage = std::make_unique<EntriesPageStorage>(
      std::map<std::string, std::vector<uint8_t>>{{key, std::vector<uint8_t>{7}}},
      std::get<0>(GetParam()), std::get<1>(GetParam()), std::get<2>(GetParam()),
      std::get<3>(GetParam()), test_loop().dispatcher());
  storage->fail_after_successful_calls(1);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, storage.get());

  ActivePageManager active_page_manager(&environment_, std::move(storage), nullptr,
                                        std::move(merger),
                                        ActivePageManager::PageStorageState::NEEDS_SYNC);
  active_page_manager.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  active_page_manager.GetValue(
      storage::IdAndParentIdsCommit{convert::ToString(storage::kFirstPageCommitId), {}}, key,
      callback::Capture(callback::SetWhenCalled(&callback_called), &status, &value));
  RunLoopUntilIdle();

  ASSERT_TRUE(callback_called);
  EXPECT_THAT(status, Ne(Status::OK));
  EXPECT_TRUE(on_discardable_called);
}

INSTANTIATE_TEST_SUITE_P(ActivePageManagerTest, IdsAndParentIdsPageStorageActivePageManagerTest,
                         Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS));

INSTANTIATE_TEST_SUITE_P(ActivePageManagerTest,
                         IdsAndParentIdsPageStoragePlusFailureIntegerActivePageManagerTest,
                         Combine(Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS),
                                 Range<size_t>(0, 9)));

INSTANTIATE_TEST_SUITE_P(ActivePageManagerTest, EntriesPageStorageActivePageManagerTest,
                         Combine(Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS),
                                 Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS),
                                 Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS),
                                 Values(Synchrony::ASYNCHRONOUS, Synchrony::SYNCHRONOUS)));

}  // namespace
}  // namespace ledger
