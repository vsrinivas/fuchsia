// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_manager.h"

#include <memory>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "gtest/gtest.h"
#include "lib/backoff/exponential_backoff.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/merging/merge_resolver.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/storage/testing/commit_empty_impl.h"
#include "peridot/bin/ledger/sync_coordinator/public/ledger_sync.h"
#include "peridot/bin/ledger/sync_coordinator/testing/page_sync_empty_impl.h"

namespace ledger {
namespace {

std::unique_ptr<MergeResolver> GetDummyResolver(Environment* environment,
                                                storage::PageStorage* storage) {
  return std::make_unique<MergeResolver>(
      [] {}, environment, storage,
      std::make_unique<backoff::ExponentialBackoff>(zx::sec(0), 1u,
                                                    zx::sec(0)));
}

std::string ToString(const fuchsia::mem::BufferPtr& vmo) {
  std::string value;
  bool status = fsl::StringFromVmo(*vmo, &value);
  FXL_DCHECK(status);
  return value;
}

class FakePageSync : public sync_coordinator::PageSyncEmptyImpl {
 public:
  void Start() override { start_called = true; }

  void SetOnBacklogDownloaded(
      fxl::Closure on_backlog_downloaded_callback) override {
    this->on_backlog_downloaded_callback =
        std::move(on_backlog_downloaded_callback);
  }

  void SetOnIdle(fxl::Closure on_idle) override { this->on_idle = on_idle; }

  void SetSyncWatcher(sync_coordinator::SyncStateWatcher* watcher) override {
    this->watcher = watcher;
  }

  bool start_called = false;
  sync_coordinator::SyncStateWatcher* watcher = nullptr;
  fxl::Closure on_backlog_downloaded_callback;
  fxl::Closure on_idle;
};

class PageManagerTest : public gtest::TestLoopFixture {
 public:
  PageManagerTest()
      : environment_(EnvironmentBuilder().SetAsync(dispatcher()).Build()) {}
  ~PageManagerTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id_ = storage::PageId(kPageIdSize, 'a');
  }

  void DrainLoop() {
    RunLoopRepeatedlyFor(storage::fake::kFakePageStorageDelay);
  }

  storage::PageId page_id_;
  Environment environment_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageManagerTest);
};

TEST_F(PageManagerTest, OnEmptyCallback) {
  bool on_empty_called = false;
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  auto merger = GetDummyResolver(&environment_, storage.get());
  PageManager page_manager(&environment_, std::move(storage), nullptr,
                           std::move(merger),
                           PageManager::PageStorageState::NEEDS_SYNC);
  page_manager.set_on_empty(callback::SetWhenCalled(&on_empty_called));
  DrainLoop();
  EXPECT_FALSE(on_empty_called);

  bool called;
  Status status;
  PagePtr page1;
  PagePtr page2;
  page_manager.BindPage(
      page1.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  page_manager.BindPage(
      page2.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  page1.Unbind();
  page2.Unbind();
  DrainLoop();
  EXPECT_TRUE(on_empty_called);
  EXPECT_TRUE(page_manager.IsEmpty());

  on_empty_called = false;
  PagePtr page3;
  page_manager.BindPage(
      page3.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
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
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  auto merger = GetDummyResolver(&environment_, storage.get());
  auto page_manager = std::make_unique<PageManager>(
      &environment_, std::move(storage), nullptr, std::move(merger),
      PageManager::PageStorageState::NEEDS_SYNC);

  bool called;
  Status status;
  PagePtr page;
  page_manager->BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  bool page_closed;
  page.set_error_handler(callback::SetWhenCalled(&page_closed));

  page_manager.reset();
  DrainLoop();
  EXPECT_TRUE(page_closed);
}

TEST_F(PageManagerTest, OnEmptyCallbackWithWatcher) {
  bool on_empty_called = false;
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
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
  Status status;
  PagePtr page1;
  PagePtr page2;
  page_manager.BindPage(
      page1.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  page_manager.BindPage(
      page2.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  page1->Put(convert::ToArray("key1"), convert::ToArray("value1"),
             callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageWatcherPtr watcher;
  fidl::InterfaceRequest<PageWatcher> watcher_request = watcher.NewRequest();
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(
      snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
      std::move(watcher),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

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
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
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
  Status status;
  PagePtr page;
  page_manager.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  // The page shouldn't be bound until sync backlog is downloaded.
  DrainLoop();
  EXPECT_FALSE(called);

  page->GetId(callback::Capture(callback::SetWhenCalled(&called),
                                static_cast<PageId*>(nullptr)));
  DrainLoop();
  EXPECT_FALSE(called);

  fake_page_sync_ptr->on_backlog_downloaded_callback();

  // BindPage callback can now be executed; GetId callback should then be
  // called.
  DrainLoop();
  EXPECT_TRUE(called);

  // Check that a second call on the same manager is not delayed.
  page.Unbind();
  page_manager.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  page->GetId(callback::Capture(callback::SetWhenCalled(&called),
                                static_cast<PageId*>(nullptr)));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(PageManagerTest, DelayBindingUntilSyncTimeout) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
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
  Status status;
  PagePtr page;
  page_manager.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  page->GetId(callback::Capture(callback::SetWhenCalled(&called),
                                static_cast<PageId*>(nullptr)));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(PageManagerTest, ExitWhenSyncFinishes) {
  auto fake_page_sync = std::make_unique<FakePageSync>();
  auto fake_page_sync_ptr = fake_page_sync.get();
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
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
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
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
  Status status;
  PagePtr page;
  page_manager.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  // The page should be bound immediately.
  DrainLoop();
  ASSERT_TRUE(called);
  ASSERT_EQ(Status::OK, status);

  page->GetId(callback::Capture(callback::SetWhenCalled(&called),
                                static_cast<PageId*>(nullptr)));
  DrainLoop();
  EXPECT_TRUE(called);
}

TEST_F(PageManagerTest, GetHeadCommitEntries) {
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  auto merger = GetDummyResolver(&environment_, storage.get());
  PageManager page_manager(&environment_, std::move(storage), nullptr,
                           std::move(merger),
                           PageManager::PageStorageState::NEEDS_SYNC);
  bool called;
  Status status;
  PagePtr page;
  page_manager.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ledger_internal::PageDebugPtr page_debug;
  page_manager.BindPageDebug(
      page_debug.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  std::string key1("001-some_key");
  std::string value1("a small value");

  page->Put(convert::ToArray(key1), convert::ToArray(value1),
            callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fidl::VectorPtr<ledger_internal::CommitId> heads1;
  page_debug->GetHeadCommitsIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &heads1));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(1u, heads1->size());

  std::string key2("002-some_key2");
  std::string value2("another value");

  page->Put(convert::ToArray(key2), convert::ToArray(value2),
            callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fidl::VectorPtr<ledger_internal::CommitId> heads2;
  page_debug->GetHeadCommitsIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &heads2));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(1u, heads2->size());

  EXPECT_NE(convert::ToString(heads1->at(0).id),
            convert::ToString(heads2->at(0).id));

  PageSnapshotPtr snapshot1;
  page_debug->GetSnapshot(
      std::move(heads1->at(0)), snapshot1.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  PageSnapshotPtr snapshot2;
  page_debug->GetSnapshot(
      std::move(heads2->at(0)), snapshot2.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fidl::VectorPtr<Entry> expected_entries1;
  std::unique_ptr<Token> next_token;
  snapshot1->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &expected_entries1, &next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(1u, expected_entries1->size());
  EXPECT_EQ(key1, convert::ToString(expected_entries1->at(0).key));
  EXPECT_EQ(value1, ToString(expected_entries1->at(0).value));

  fidl::VectorPtr<Entry> expected_entries2;
  snapshot2->GetEntries(
      fidl::VectorPtr<uint8_t>::New(0), nullptr,
      callback::Capture(callback::SetWhenCalled(&called), &status,
                        &expected_entries2, &next_token));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(2u, expected_entries2->size());
  EXPECT_EQ(key1, convert::ToString(expected_entries2->at(0).key));
  EXPECT_EQ(value1, ToString(expected_entries2->at(0).value));
  EXPECT_EQ(key2, convert::ToString(expected_entries2->at(1).key));
  EXPECT_EQ(value2, ToString(expected_entries2->at(1).value));
}

TEST_F(PageManagerTest, GetCommit) {
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  auto merger = GetDummyResolver(&environment_, storage.get());
  PageManager page_manager(&environment_, std::move(storage), nullptr,
                           std::move(merger),
                           PageManager::PageStorageState::NEEDS_SYNC);
  bool called;
  Status status;
  PagePtr page;
  page_manager.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ledger_internal::PageDebugPtr page_debug;
  page_manager.BindPageDebug(
      page_debug.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  std::string key1("001-some_key");
  std::string value1("a small value");

  page->Put(convert::ToArray(key1), convert::ToArray(value1),
            callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fidl::VectorPtr<ledger_internal::CommitId> heads1;
  page_debug->GetHeadCommitsIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &heads1));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(1u, heads1->size());

  std::string key2("002-some_key2");
  std::string value2("another value");

  page->Put(convert::ToArray(key2), convert::ToArray(value2),
            callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  fidl::VectorPtr<ledger_internal::CommitId> heads2;
  page_debug->GetHeadCommitsIds(
      callback::Capture(callback::SetWhenCalled(&called), &status, &heads2));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(1u, heads2->size());

  ledger_internal::CommitPtr commit_struct;
  ledger_internal::CommitId currHeadCommit = fidl::Clone(heads2->at(0));
  page_debug->GetCommit(std::move(currHeadCommit),
                        callback::Capture(callback::SetWhenCalled(&called),
                                          &status, &commit_struct));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(heads2->at(0).id, commit_struct->commit_id.id);
  EXPECT_EQ(1u, commit_struct->parents_ids->size());
  EXPECT_EQ(1u, commit_struct->generation);
  EXPECT_EQ(heads1->at(0).id, commit_struct->parents_ids->at(0).id);
}

TEST_F(PageManagerTest, GetCommitError) {
  auto storage = std::make_unique<storage::fake::FakePageStorage>(page_id_);
  auto merger = GetDummyResolver(&environment_, storage.get());
  PageManager page_manager(&environment_, std::move(storage), nullptr,
                           std::move(merger),
                           PageManager::PageStorageState::NEEDS_SYNC);
  bool called;
  Status status;
  PagePtr page;
  page_manager.BindPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ledger_internal::PageDebugPtr page_debug;
  page_manager.BindPageDebug(
      page_debug.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ledger_internal::CommitPtr commit_struct;
  page_debug->GetCommit({convert::ToArray("fake_commit_id")},
                        callback::Capture(callback::SetWhenCalled(&called),
                                          &status, &commit_struct));
  DrainLoop();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INVALID_ARGUMENT, status);
}

}  // namespace
}  // namespace ledger
