// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/fake/fake_ledger_storage.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"
#include "src/ledger/bin/storage/public/ledger_storage.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/testing/fake_ledger_sync.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

constexpr absl::string_view kLedgerName = "ledger_under_test";

PageId RandomId(const Environment& environment) {
  PageId result;
  environment.random()->Draw(&result.id);
  return result;
}

class LedgerManagerTest : public TestWithEnvironment {
 public:
  LedgerManagerTest() = default;
  LedgerManagerTest(const LedgerManagerTest&) = delete;
  LedgerManagerTest& operator=(const LedgerManagerTest&) = delete;
  ~LedgerManagerTest() override = default;

  // gtest::TestWithEnvironment:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    std::unique_ptr<storage::fake::FakeLedgerStorage> storage =
        std::make_unique<storage::fake::FakeLedgerStorage>(&environment_);
    storage_ptr = storage.get();
    std::unique_ptr<sync_coordinator::FakeLedgerSync> sync =
        std::make_unique<sync_coordinator::FakeLedgerSync>();
    sync_ptr = sync.get();
    disk_cleanup_manager_ = std::make_unique<FakeDiskCleanupManager>();
    ledger_manager_ = std::make_unique<LedgerManager>(
        &environment_, convert::ToString(kLedgerName),
        std::make_unique<encryption::FakeEncryptionService>(dispatcher()), std::move(storage),
        std::move(sync), std::vector<PageUsageListener*>{disk_cleanup_manager_.get()});
    ResetLedgerPtr();
  }

  void ResetLedgerPtr() { ledger_manager_->BindLedger(ledger_.NewRequest()); }

 protected:
  storage::fake::FakeLedgerStorage* storage_ptr;
  sync_coordinator::FakeLedgerSync* sync_ptr;
  std::unique_ptr<FakeDiskCleanupManager> disk_cleanup_manager_;
  std::unique_ptr<LedgerManager> ledger_manager_;
  LedgerPtr ledger_;
};

class StubConflictResolverFactory : public ConflictResolverFactory {
 public:
  explicit StubConflictResolverFactory(fidl::InterfaceRequest<ConflictResolverFactory> request)
      : binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { disconnected = true; });
  }

  bool disconnected = false;

 private:
  void GetPolicy(PageId page_id, fit::function<void(MergePolicy)> callback) override {}

  void NewConflictResolver(PageId page_id,
                           fidl::InterfaceRequest<ConflictResolver> resolver) override {}

  fidl::Binding<ConflictResolverFactory> binding_;
};

// Verifies that LedgerImpl proxies vended by LedgerManager work correctly,
// that is, make correct calls to ledger storage.
TEST_F(LedgerManagerTest, LedgerImpl) {
  EXPECT_EQ(storage_ptr->create_page_calls.size(), 0u);
  EXPECT_EQ(storage_ptr->get_page_calls.size(), 0u);

  PagePtr page;
  storage_ptr->should_get_page_fail = true;
  ledger_->GetPage(nullptr, page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(storage_ptr->create_page_calls.size(), 1u);
  EXPECT_EQ(storage_ptr->get_page_calls.size(), 1u);
  EXPECT_FALSE(ledger_);
  page.Unbind();
  storage_ptr->ClearCalls();

  ResetLedgerPtr();
  storage_ptr->should_get_page_fail = true;
  ledger_->GetRootPage(page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(storage_ptr->create_page_calls.size(), 1u);
  EXPECT_EQ(storage_ptr->get_page_calls.size(), 1u);
  EXPECT_FALSE(ledger_);
  page.Unbind();
  storage_ptr->ClearCalls();

  ResetLedgerPtr();
  PageId id = RandomId(environment_);
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(storage_ptr->create_page_calls.size(), 1u);
  ASSERT_EQ(storage_ptr->get_page_calls.size(), 1u);
  EXPECT_FALSE(ledger_);
  EXPECT_EQ(storage_ptr->get_page_calls[0], convert::ToString(id.id));
  page.Unbind();
  storage_ptr->ClearCalls();
}

// Verifies that deleting the LedgerManager closes the channels connected to
// LedgerImpl.
TEST_F(LedgerManagerTest, DeletingLedgerManagerClosesConnections) {
  bool ledger_closed = false;
  ledger_.set_error_handler([this, &ledger_closed](zx_status_t status) {
    ledger_closed = true;
    QuitLoop();
  });

  ledger_manager_.reset();
  RunLoopUntilIdle();
  EXPECT_TRUE(ledger_closed);
}

TEST_F(LedgerManagerTest, OnDiscardableCalled) {
  bool on_discardable_called;
  ledger_manager_->SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(LedgerManagerTest, OnDiscardableCalledWhenLastDetacherCalled) {
  bool on_discardable_called;
  ledger_manager_->SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  auto first_detacher = ledger_manager_->CreateDetacher();
  auto second_detacher = ledger_manager_->CreateDetacher();

  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  first_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  auto third_detacher = ledger_manager_->CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  second_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  third_detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

// Verifies that the LedgerManager does not call its callback while a page is
// being deleted.
TEST_F(LedgerManagerTest, NonEmptyDuringDeletion) {
  bool on_discardable_called;
  ledger_manager_->SetOnDiscardable(SetWhenCalled(&on_discardable_called));

  PageId id = RandomId(environment_);
  bool delete_page_called;
  Status delete_page_status;
  ledger_manager_->DeletePageStorage(
      id.id, Capture(SetWhenCalled(&delete_page_called), &delete_page_status));

  // Empty the Ledger manager.
  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  // Complete the deletion successfully.
  ASSERT_TRUE(storage_ptr->delete_page_storage_callback);
  storage_ptr->delete_page_storage_callback(Status::OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(delete_page_called);
  EXPECT_EQ(delete_page_status, Status::OK);
  EXPECT_TRUE(on_discardable_called);
}

// Cloud should never be queried.
TEST_F(LedgerManagerTest, GetPageDoNotCallTheCloud) {
  storage_ptr->should_get_page_fail = true;
  zx_status_t status;

  PagePtr page;
  PageId id = RandomId(environment_);
  bool called;
  storage_ptr->ClearCalls();
  // Get the root page.
  ledger_.set_error_handler(Capture(SetWhenCalled(&called), &status));
  ledger_->GetRootPage(page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(ledger_.is_bound());
  EXPECT_TRUE(called);
  EXPECT_EQ(status, ZX_ERR_IO);
  EXPECT_FALSE(ledger_);
  EXPECT_FALSE(sync_ptr->IsCalled());

  page.Unbind();
  ResetLedgerPtr();
  storage_ptr->ClearCalls();

  // Get a new page with a random id.
  ledger_.set_error_handler(Capture(SetWhenCalled(&called), &status));
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(ledger_.is_bound());
  EXPECT_TRUE(called);
  EXPECT_EQ(status, ZX_ERR_IO);
  EXPECT_FALSE(ledger_);
  EXPECT_FALSE(sync_ptr->IsCalled());

  page.Unbind();
  ResetLedgerPtr();
  storage_ptr->ClearCalls();

  // Create a new page.
  ledger_.set_error_handler(Capture(SetWhenCalled(&called), &status));
  ledger_->GetPage(nullptr, page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(ledger_.is_bound());
  EXPECT_TRUE(called);
  EXPECT_EQ(status, ZX_ERR_IO);
  EXPECT_FALSE(ledger_);
  EXPECT_FALSE(sync_ptr->IsCalled());
}

TEST_F(LedgerManagerTest, OpenPageWithDeletePageStorageInProgress) {
  PagePtr page;
  PageId id = RandomId(environment_);

  // Start deleting the page.
  bool delete_called;
  Status delete_status;
  ledger_manager_->DeletePageStorage(id.id, Capture(SetWhenCalled(&delete_called), &delete_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(delete_called);

  // Try to open the same page.
  bool get_page_done;
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  ledger_->Sync(SetWhenCalled(&get_page_done));
  RunLoopUntilIdle();
  EXPECT_FALSE(get_page_done);

  // After calling the callback registered in |DeletePageStorage| both
  // operations should terminate without an error.
  storage_ptr->delete_page_storage_callback(Status::OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(delete_called);
  EXPECT_EQ(delete_status, Status::OK);

  EXPECT_TRUE(get_page_done);
}

TEST_F(LedgerManagerTest, ChangeConflictResolver) {
  fidl::InterfaceHandle<ConflictResolverFactory> handle1;
  fidl::InterfaceHandle<ConflictResolverFactory> handle2;
  StubConflictResolverFactory factory1(handle1.NewRequest());
  StubConflictResolverFactory factory2(handle2.NewRequest());

  ledger_->SetConflictResolverFactory(std::move(handle1));
  RunLoopUntilIdle();

  ledger_->SetConflictResolverFactory(std::move(handle2));
  RunLoopUntilIdle();
  EXPECT_FALSE(factory1.disconnected);
  EXPECT_FALSE(factory2.disconnected);
}

TEST_F(LedgerManagerTest, MultipleConflictResolvers) {
  fidl::InterfaceHandle<ConflictResolverFactory> handle1;
  fidl::InterfaceHandle<ConflictResolverFactory> handle2;
  StubConflictResolverFactory factory1(handle1.NewRequest());
  StubConflictResolverFactory factory2(handle2.NewRequest());

  LedgerPtr ledger2;
  ledger_manager_->BindLedger(ledger2.NewRequest());

  ledger_->SetConflictResolverFactory(std::move(handle1));
  RunLoopUntilIdle();

  ledger2->SetConflictResolverFactory(std::move(handle2));
  RunLoopUntilIdle();
  EXPECT_FALSE(factory1.disconnected);
  EXPECT_FALSE(factory2.disconnected);
}

// Verifies that the LedgerManager triggers page sync for a page that exists and was closed.
TEST_F(LedgerManagerTest, TrySyncClosedPageSyncStarted) {
  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);
  storage::PageId page_id = convert::ToString(id.id);

  EXPECT_EQ(sync_ptr->GetSyncCallsCount(page_id), 0);

  // Opens the page and starts the sync with the cloud for the first time.
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();

  EXPECT_EQ(sync_ptr->GetSyncCallsCount(page_id), 1);

  page.Unbind();
  RunLoopUntilIdle();

  // Reopens closed page and starts the sync.
  ledger_manager_->TrySyncClosedPage(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_EQ(sync_ptr->GetSyncCallsCount(page_id), 2);
}

// Verifies that the LedgerManager does not triggers the sync for a currently open page.
TEST_F(LedgerManagerTest, TrySyncClosedPageWithOpenedPage) {
  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  PageId id = RandomId(environment_);
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);
  storage::PageId page_id = convert::ToString(id.id);

  EXPECT_EQ(sync_ptr->GetSyncCallsCount(page_id), 0);

  // Opens the page and starts the sync with the cloud for the first time.
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();

  EXPECT_EQ(sync_ptr->GetSyncCallsCount(page_id), 1);

  // Tries to reopen the already-open page.
  ledger_manager_->TrySyncClosedPage(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_EQ(sync_ptr->GetSyncCallsCount(page_id), 1);
}

class DelayingLedgerStorage : public storage::LedgerStorage {
 public:
  DelayingLedgerStorage() = default;
  ~DelayingLedgerStorage() override = default;

  void CreatePageStorage(storage::PageId page_id,
                         fit::function<void(storage::Status, std::unique_ptr<storage::PageStorage>)>
                             callback) override {
    get_page_calls_.emplace_back(std::move(page_id), std::move(callback));
  }

  void GetPageStorage(storage::PageId page_id,
                      fit::function<void(storage::Status, std::unique_ptr<storage::PageStorage>)>
                          callback) override {
    get_page_calls_.emplace_back(std::move(page_id), std::move(callback));
  }

  void DeletePageStorage(storage::PageIdView /*page_id*/,
                         fit::function<void(storage::Status)> callback) override {
    LEDGER_NOTIMPLEMENTED();
    callback(Status::NOT_IMPLEMENTED);
  }

  void ListPages(
      fit::function<void(storage::Status, std::set<storage::PageId>)> callback) override {
    LEDGER_NOTIMPLEMENTED();
    callback(Status::NOT_IMPLEMENTED, {});
  };

  std::vector<std::pair<
      storage::PageId, fit::function<void(storage::Status, std::unique_ptr<storage::PageStorage>)>>>
      get_page_calls_;
};

// Verifies that closing a page before PageStorage is ready does not leave live
// objects.
// In this test, we request a Page, but before we are able to construct a
// PageStorage, we close the connection. In that case, we should not leak
// anything.
TEST_F(LedgerManagerTest, GetPageDisconnect) {
  // Setup
  std::unique_ptr<DelayingLedgerStorage> storage = std::make_unique<DelayingLedgerStorage>();
  auto storage_ptr = storage.get();
  std::unique_ptr<sync_coordinator::FakeLedgerSync> sync =
      std::make_unique<sync_coordinator::FakeLedgerSync>();
  auto disk_cleanup_manager = std::make_unique<FakeDiskCleanupManager>();
  auto ledger_manager = std::make_unique<LedgerManager>(
      &environment_, convert::ToString(kLedgerName),
      std::make_unique<encryption::FakeEncryptionService>(dispatcher()), std::move(storage),
      std::move(sync), std::vector<PageUsageListener*>{disk_cleanup_manager_.get()});

  LedgerPtr ledger;
  ledger_manager->BindLedger(ledger.NewRequest());
  RunLoopUntilIdle();

  PageId id = RandomId(environment_);

  PagePtr page1;
  ledger->GetPage(fidl::MakeOptional(id), page1.NewRequest());
  RunLoopUntilIdle();

  ASSERT_THAT(storage_ptr->get_page_calls_, testing::SizeIs(1));
  page1.Unbind();
  RunLoopUntilIdle();

  auto it = storage_ptr->get_page_calls_.begin();
  it->second(storage::Status::OK,
             std::make_unique<storage::fake::FakePageStorage>(&environment_, it->first));
  RunLoopUntilIdle();

  PagePtr page2;
  ledger->GetPage(fidl::MakeOptional(id), page2.NewRequest());
  RunLoopUntilIdle();

  // We verify that we ask again for a new PageStorage, the previous one was
  // correctly discarded.
  EXPECT_THAT(storage_ptr->get_page_calls_, testing::SizeIs(2));
}

}  // namespace
}  // namespace ledger
