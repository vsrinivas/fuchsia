// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager.h"

#include <memory>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/macros.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/merging/merge_resolver.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/commit_empty_impl.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/testing/page_sync_empty_impl.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

std::unique_ptr<MergeResolver> GetDummyResolver(Environment* environment,
                                                storage::PageStorage* storage) {
  return std::make_unique<MergeResolver>(
      [] {}, environment, storage,
      std::make_unique<backoff::ExponentialBackoff>(
          zx::sec(0), 1u, zx::sec(0),
          environment->random()->NewBitGenerator<uint64_t>()));
}

class FakePageSync : public sync_coordinator::PageSyncEmptyImpl {
 public:
  void Start() override { start_called = true; }

  void SetOnBacklogDownloaded(
      fit::closure on_backlog_downloaded_callback) override {
    this->on_backlog_downloaded_callback =
        std::move(on_backlog_downloaded_callback);
  }

  void SetOnIdle(fit::closure on_idle) override {
    this->on_idle = std::move(on_idle);
  }

  void SetSyncWatcher(sync_coordinator::SyncStateWatcher* watcher) override {
    this->watcher = watcher;
  }

  bool start_called = false;
  sync_coordinator::SyncStateWatcher* watcher = nullptr;
  fit::closure on_backlog_downloaded_callback;
  fit::closure on_idle;
};

class PageManagerTest : public TestWithEnvironment {
 public:
  PageManagerTest() {}
  ~PageManagerTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id_ = storage::PageId(::fuchsia::ledger::kPageIdSize, 'a');
  }

  void DrainLoop() {
    RunLoopRepeatedlyFor(storage::fake::kFakePageStorageDelay);
  }

  std::unique_ptr<storage::PageStorage> MakeStorage() {
    return std::make_unique<storage::fake::FakePageStorage>(&environment_,
                                                            page_id_);
  }

  storage::PageId page_id_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageManagerTest);
};

TEST_F(PageManagerTest, OnEmptyCallback) {
  bool on_empty_called = false;
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  PageManager page_manager(&environment_, std::move(storage), nullptr,
                           std::move(merger),
                           PageManager::PageStorageState::NEEDS_SYNC);
  page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  DrainLoop();
  EXPECT_FALSE(on_empty_called);

  bool called;
  storage::Status status;
  PagePtr page1;
  PagePtr page2;

  auto page_impl1 = std::make_unique<PageImpl>(page_id_, page1.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl1),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, status);

  auto page_impl2 = std::make_unique<PageImpl>(page_id_, page2.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl2),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, status);

  page1.Unbind();
  page2.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_empty_called);
  EXPECT_TRUE(page_manager.IsEmpty());

  on_empty_called = false;
  PagePtr page3;
  auto page_impl3 = std::make_unique<PageImpl>(page_id_, page3.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl3),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, status);
  EXPECT_FALSE(page_manager.IsEmpty());

  page3.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_empty_called);
  EXPECT_TRUE(page_manager.IsEmpty());

  on_empty_called = false;
  PageSnapshotPtr snapshot;
  page_manager.BindPageSnapshot(
      std::make_unique<const storage::CommitEmptyImpl>(), snapshot.NewRequest(),
      "");
  DrainLoop();
  EXPECT_FALSE(page_manager.IsEmpty());
  snapshot.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageManagerTest, DeletingPageManagerClosesConnections) {
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  auto page_manager = std::make_unique<PageManager>(
      &environment_, std::move(storage), nullptr, std::move(merger),
      PageManager::PageStorageState::NEEDS_SYNC);

  bool called;
  storage::Status status;
  PagePtr page;
  auto page_impl = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  page_manager->AddPageImpl(
      std::move(page_impl),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, status);
  bool page_closed;
  page.set_error_handler([callback = callback::SetWhenCalled(&page_closed)](
                             zx_status_t status) { callback(); });

  page_manager.reset();
  DrainLoop();
  EXPECT_TRUE(page_closed);
}

TEST_F(PageManagerTest, OnEmptyCallbackWithWatcher) {
  bool on_empty_called = false;
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());
  PageManager page_manager(&environment_, std::move(storage), nullptr,
                           std::move(merger),
                           PageManager::PageStorageState::NEEDS_SYNC);
  page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  DrainLoop();
  // PageManager is empty, but on_empty should not have be called, yet.
  EXPECT_FALSE(on_empty_called);
  EXPECT_TRUE(page_manager.IsEmpty());

  bool called;
  storage::Status internal_status;
  PagePtr page1;
  PagePtr page2;
  auto page_impl1 = std::make_unique<PageImpl>(page_id_, page1.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl1),
      callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, internal_status);

  auto page_impl2 = std::make_unique<PageImpl>(page_id_, page2.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl2),
      callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, internal_status);

  page1->PutNew(convert::ToArray("key1"), convert::ToArray("value1"));

  PageWatcherPtr watcher;
  fidl::InterfaceRequest<PageWatcher> watcher_request = watcher.NewRequest();
  PageSnapshotPtr snapshot;
  page1->GetSnapshotNew(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                        std::move(watcher));

  page1.Unbind();
  page2.Unbind();
  snapshot.Unbind();
  DrainLoop();
  EXPECT_FALSE(page_manager.IsEmpty());
  EXPECT_FALSE(on_empty_called);

  watcher_request.TakeChannel();
  DrainLoop();
  EXPECT_TRUE(page_manager.IsEmpty());
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageManagerTest, DelayBindingUntilSyncBacklogDownloaded) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  PageManager page_manager(&environment_, std::move(storage),
                           std::move(fake_page_sync), std::move(merger),
                           PageManager::PageStorageState::NEEDS_SYNC);

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_TRUE(fake_page_sync_ptr->start_called);
  EXPECT_TRUE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  bool called;
  storage::Status internal_status;
  PagePtr page;
  auto page_impl1 = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl1),
      callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  // The page should be bound, but except from GetId, no other method should
  // be executed, until the sync backlog is downloaded.
  DrainLoop();
  EXPECT_FALSE(called);

  PageId found_page_id;
  page->GetId(
      callback::Capture(callback::SetWhenCalled(&called), &found_page_id));
  DrainLoop();
  EXPECT_TRUE(called);
  PageId expected_page_id;
  convert::ToArray(page_id_, &expected_page_id.id);
  EXPECT_EQ(expected_page_id.id, found_page_id.id);

  // Clear should not be executed.
  page->ClearNew();

  fake_page_sync_ptr->on_backlog_downloaded_callback();
  // BindPage callback can now be executed; Clear callback should then be
  // called.
  DrainLoop();
  EXPECT_TRUE(called);

  // Check that a second call on the same manager is not delayed.
  page.Unbind();
  auto page_impl2 = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl2),
      callback::Capture(callback::SetWhenCalled(&called), &internal_status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, internal_status);

  page->GetId(
      callback::Capture(callback::SetWhenCalled(&called), &std::ignore));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(PageManagerTest, DelayBindingUntilSyncTimeout) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  PageManager page_manager(
      &environment_, std::move(storage), std::move(fake_page_sync),
      std::move(merger), PageManager::PageStorageState::NEEDS_SYNC, zx::sec(0));

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_TRUE(fake_page_sync_ptr->start_called);
  EXPECT_TRUE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  bool called;
  storage::Status status;
  PagePtr page;
  auto page_impl = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, status);

  page->GetId(
      callback::Capture(callback::SetWhenCalled(&called), &std::ignore));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(PageManagerTest, ExitWhenSyncFinishes) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  PageManager page_manager(
      &environment_, std::move(storage), std::move(fake_page_sync),
      std::move(merger), PageManager::PageStorageState::NEEDS_SYNC, zx::sec(0));

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);

  bool called;
  page_manager.set_on_empty(callback::SetWhenCalled(&called));

  async::PostTask(dispatcher(),
                  [fake_page_sync_ptr] { fake_page_sync_ptr->on_idle(); });

  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_TRUE(page_manager.IsEmpty());
}

TEST_F(PageManagerTest, DontDelayBindingWithLocalPageStorage) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = MakeStorage();
  auto merger = GetDummyResolver(&environment_, storage.get());

  EXPECT_EQ(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_FALSE(fake_page_sync_ptr->start_called);
  EXPECT_FALSE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  PageManager page_manager(
      &environment_, std::move(storage), std::move(fake_page_sync),
      std::move(merger), PageManager::PageStorageState::AVAILABLE,
      // Use a long timeout to ensure the test does not hit it.
      zx::sec(3600));

  EXPECT_NE(nullptr, fake_page_sync_ptr->watcher);
  EXPECT_TRUE(fake_page_sync_ptr->start_called);
  EXPECT_TRUE(fake_page_sync_ptr->on_backlog_downloaded_callback);

  bool called;
  storage::Status status;
  PagePtr page;
  auto page_impl = std::make_unique<PageImpl>(page_id_, page.NewRequest());
  page_manager.AddPageImpl(
      std::move(page_impl),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  // The page should be bound immediately.
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(storage::Status::OK, status);

  page->GetId(
      callback::Capture(callback::SetWhenCalled(&called), &std::ignore));
  DrainLoop();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace ledger
