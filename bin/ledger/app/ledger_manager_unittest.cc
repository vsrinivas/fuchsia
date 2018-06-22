// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include <lib/async/cpp/task.h>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/callback/waiter.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/random/rand.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/page_eviction_manager_impl.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/public/ledger_storage.h"
#include "peridot/bin/ledger/sync_coordinator/public/ledger_sync.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
namespace {

ledger::PageId RandomId() {
  ledger::PageId result;
  fxl::RandBytes(&result.id[0], result.id.count());
  return result;
}

class FakeLedgerStorage : public storage::LedgerStorage {
 public:
  explicit FakeLedgerStorage(async_t* async) : async_(async) {}
  ~FakeLedgerStorage() override {}

  void CreatePageStorage(
      storage::PageId page_id,
      std::function<void(storage::Status,
                         std::unique_ptr<storage::PageStorage>)>
          callback) override {
    create_page_calls.push_back(std::move(page_id));
    callback(storage::Status::IO_ERROR, nullptr);
  }

  void GetPageStorage(storage::PageId page_id,
                      std::function<void(storage::Status,
                                         std::unique_ptr<storage::PageStorage>)>
                          callback) override {
    get_page_calls.push_back(page_id);
    async::PostTask(async_, [this, callback, page_id]() mutable {
      if (should_get_page_fail) {
        callback(storage::Status::NOT_FOUND, nullptr);
      } else {
        callback(storage::Status::OK,
                 std::make_unique<storage::fake::FakePageStorage>(
                     std::move(page_id)));
      }
    });
  }

  bool DeletePageStorage(storage::PageIdView page_id) override {
    return false;
  }

  void ClearCalls() {
    create_page_calls.clear();
    get_page_calls.clear();
  }

  bool should_get_page_fail = false;
  std::vector<storage::PageId> create_page_calls;
  std::vector<storage::PageId> get_page_calls;

 private:
  async_t* const async_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerStorage);
};

class FakeLedgerSync : public sync_coordinator::LedgerSync {
 public:
  FakeLedgerSync() {}
  ~FakeLedgerSync() override {}

  std::unique_ptr<sync_coordinator::PageSync> CreatePageSync(
      storage::PageStorage* /*page_storage*/,
      storage::PageSyncClient* /*page_sync_client*/,
      fxl::Closure /*error_callback*/) override {
    called = true;
    return nullptr;
  }

  bool called = false;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerSync);
};

class LedgerManagerTest : public gtest::TestLoopFixture {
 public:
  LedgerManagerTest()
      : environment_(EnvironmentBuilder().SetAsync(dispatcher()).Build()) {}

  // gtest::TestLoopFixture:
  void SetUp() override {
    gtest::TestLoopFixture::SetUp();
    std::unique_ptr<FakeLedgerStorage> storage =
        std::make_unique<FakeLedgerStorage>(dispatcher());
    storage_ptr = storage.get();
    std::unique_ptr<FakeLedgerSync> sync = std::make_unique<FakeLedgerSync>();
    sync_ptr = sync.get();
    page_eviction_manager_ = std::make_unique<PageEvictionManagerImpl>();
    FXL_CHECK(page_eviction_manager_->Init() == Status::OK);
    ledger_manager_ = std::make_unique<LedgerManager>(
        &environment_, "test_ledger",
        std::make_unique<encryption::FakeEncryptionService>(dispatcher()),
        std::move(storage), std::move(sync), page_eviction_manager_.get());
    ledger_manager_->BindLedger(ledger_.NewRequest());
    ledger_manager_->BindLedgerDebug(ledger_debug_.NewRequest());
  }

 protected:
  Environment environment_;
  FakeLedgerStorage* storage_ptr;
  FakeLedgerSync* sync_ptr;
  std::unique_ptr<PageEvictionManagerImpl> page_eviction_manager_;
  std::unique_ptr<LedgerManager> ledger_manager_;
  LedgerPtr ledger_;
  ledger_internal::LedgerDebugPtr ledger_debug_;
};

// Verifies that LedgerImpl proxies vended by LedgerManager work correctly,
// that is, make correct calls to ledger storage.
TEST_F(LedgerManagerTest, LedgerImpl) {
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());

  PagePtr page;
  storage_ptr->should_get_page_fail = true;
  ledger_->GetPage(nullptr, page.NewRequest(), [this](Status) { QuitLoop(); });
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  page.Unbind();
  storage_ptr->ClearCalls();

  storage_ptr->should_get_page_fail = true;
  ledger_->GetRootPage(page.NewRequest(), [this](Status) { QuitLoop(); });
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  page.Unbind();
  storage_ptr->ClearCalls();

  ledger::PageId id = RandomId();
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest(),
                   [this](Status) { QuitLoop(); });
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(convert::ToString(id.id), storage_ptr->get_page_calls[0]);
  page.Unbind();
  storage_ptr->ClearCalls();
}

// Verifies that deleting the LedgerManager closes the channels connected to
// LedgerImpl.
TEST_F(LedgerManagerTest, DeletingLedgerManagerClosesConnections) {
  bool ledger_closed = false;
  ledger_.set_error_handler([this, &ledger_closed] {
    ledger_closed = true;
    QuitLoop();
  });

  ledger_manager_.reset();
  RunLoopUntilIdle();
  EXPECT_TRUE(ledger_closed);
}

TEST_F(LedgerManagerTest, OnEmptyCalled) {
  bool on_empty_called;
  ledger_manager_->set_on_empty(callback::SetWhenCalled(&on_empty_called));

  ledger_.Unbind();
  ledger_debug_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

// Verifies that two successive calls to GetPage do not create 2 storages.
TEST_F(LedgerManagerTest, CallGetPageTwice) {
  PagePtr page;
  ledger::PageId id = RandomId();

  uint8_t calls = 0;
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest(),
                   [&calls](Status) { calls++; });
  page.Unbind();
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest(),
                   [&calls](Status) { calls++; });
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(2u, calls);
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(convert::ToString(id.id), storage_ptr->get_page_calls[0]);
}

// Cloud should never be queried.
TEST_F(LedgerManagerTest, GetPageDoNotCallTheCloud) {
  storage_ptr->should_get_page_fail = true;
  Status status;

  PagePtr page;
  ledger::PageId id = RandomId();
  bool called;
  // Get the root page.
  storage_ptr->ClearCalls();
  page.Unbind();
  ledger_->GetRootPage(
      page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);

  // Get a new page with a random id.
  storage_ptr->ClearCalls();
  page.Unbind();
  ledger_->GetPage(
      fidl::MakeOptional(id), page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);

  // Create a new page.
  storage_ptr->ClearCalls();
  page.Unbind();
  ledger_->GetPage(
      nullptr, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
  EXPECT_FALSE(sync_ptr->called);
}

// Verifies that LedgerDebugImpl proxy vended by LedgerManager works correctly.
TEST_F(LedgerManagerTest, CallGetPagesList) {
  std::vector<PagePtr> pages(3);
  std::vector<ledger::PageId> ids;

  for (size_t i = 0; i < pages.size(); ++i) {
    ids.push_back(RandomId());
  }

  Status status;

  fidl::VectorPtr<ledger::PageId> actual_pages_list;

  EXPECT_EQ(0u, actual_pages_list->size());

  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  for (size_t i = 0; i < pages.size(); ++i) {
    ledger_->GetPage(fidl::MakeOptional(ids[i]), pages[i].NewRequest(),
                     waiter->NewCallback());
  }

  bool called;
  waiter->Finalize(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  ledger_debug_->GetPagesList(
      callback::Capture(callback::SetWhenCalled(&called), &actual_pages_list));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(pages.size(), actual_pages_list->size());

  std::sort(ids.begin(), ids.end(),
            [](const ledger::PageId& lhs, const ledger::PageId& rhs) {
              return convert::ToStringView(lhs.id) <
                     convert::ToStringView(rhs.id);
            });
  for (size_t i = 0; i < ids.size(); i++)
    EXPECT_EQ(ids[i].id, actual_pages_list->at(i).id);
}

}  // namespace
}  // namespace ledger
