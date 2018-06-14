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
#include "lib/fxl/functional/closure.h"
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

class DelayIsSyncedCallbackFakePageStorage
    : public storage::fake::FakePageStorage {
 public:
  explicit DelayIsSyncedCallbackFakePageStorage(storage::PageId id)
      : storage::fake::FakePageStorage(id) {}
  ~DelayIsSyncedCallbackFakePageStorage() override {}

  void IsSynced(std::function<void(storage::Status, bool)> callback) override {
    is_synced_callback_ = std::move(callback);
    if (!delay_callback_) {
      CallIsSyncedCallback();
    }
  }

  void DelayIsSyncedCallback(bool delay_callback) {
    delay_callback_ = delay_callback;
  }

  void CallIsSyncedCallback() {
    storage::fake::FakePageStorage::IsSynced(std::move(is_synced_callback_));
  }

 private:
  std::function<void(storage::Status, bool)> is_synced_callback_;
  bool delay_callback_ = false;
};

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
        auto fake_page_storage =
            std::make_unique<DelayIsSyncedCallbackFakePageStorage>(page_id);
        // If the page was opened before, restore the previous sync state.
        fake_page_storage->set_syned(synced_pages_.find(page_id) !=
                                     synced_pages_.end());
        fake_page_storage->DelayIsSyncedCallback(
            pages_with_delayed_callback.find(page_id) !=
            pages_with_delayed_callback.end());
        page_storages_[std::move(page_id)] = fake_page_storage.get();
        callback(storage::Status::OK, std::move(fake_page_storage));
      }
    });
  }

  bool DeletePageStorage(storage::PageIdView page_id) override { return false; }

  void ClearCalls() {
    create_page_calls.clear();
    get_page_calls.clear();
    page_storages_.clear();
  }

  void DelayIsSyncedCallback(storage::PageIdView page_id, bool delay_callback) {
    if (delay_callback) {
      pages_with_delayed_callback.insert(page_id.ToString());
    } else {
      auto it = pages_with_delayed_callback.find(page_id.ToString());
      if (it != pages_with_delayed_callback.end()) {
        pages_with_delayed_callback.erase(it);
      }
    }
    auto it = page_storages_.find(page_id.ToString());
    FXL_CHECK(it != page_storages_.end());
    it->second->DelayIsSyncedCallback(delay_callback);
  }

  void CallIsSyncedCallback(storage::PageIdView page_id) {
    auto it = page_storages_.find(page_id.ToString());
    FXL_CHECK(it != page_storages_.end());
    it->second->CallIsSyncedCallback();
  }

  void set_page_storage_synced(storage::PageIdView page_id, bool is_synced) {
    storage::PageId page_id_string = page_id.ToString();
    if (is_synced) {
      synced_pages_.insert(page_id_string);
    } else {
      auto it = synced_pages_.find(page_id_string);
      if (it != synced_pages_.end()) {
        synced_pages_.erase(it);
      }
    }

    FXL_CHECK(page_storages_.find(page_id_string) != page_storages_.end());
    page_storages_[page_id_string]->set_syned(is_synced);
  }

  bool should_get_page_fail = false;
  std::vector<storage::PageId> create_page_calls;
  std::vector<storage::PageId> get_page_calls;

 private:
  async_t* const async_;
  std::map<storage::PageId, DelayIsSyncedCallbackFakePageStorage*>
      page_storages_;
  std::set<storage::PageId> synced_pages_;
  std::set<storage::PageId> pages_with_delayed_callback;

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

  ~LedgerManagerTest() {}

  // gtest::TestLoopFixture:
  void SetUp() override {
    gtest::TestLoopFixture::SetUp();
    std::unique_ptr<FakeLedgerStorage> storage =
        std::make_unique<FakeLedgerStorage>(dispatcher());
    storage_ptr = storage.get();
    std::unique_ptr<FakeLedgerSync> sync = std::make_unique<FakeLedgerSync>();
    sync_ptr = sync.get();
    page_eviction_manager_ = std::make_unique<PageEvictionManagerImpl>(
        environment_.coroutine_service());
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

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerManagerTest);
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

TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckNotFound) {
  bool called;
  Status status;
  PageClosedAndSynced is_closed_and_synced;

  // Check for a page that doesn't exist.
  storage_ptr->should_get_page_fail = true;
  ledger_manager_->PageIsClosedAndSynced(
      "page_id", callback::Capture(callback::SetWhenCalled(&called), &status,
                                   &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PAGE_NOT_FOUND, status);
}

// Check for a page that exists, is synced and open. PageIsClosedAndSynced
// should be false.
TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckClosed) {
  bool called;
  Status status;
  PageClosedAndSynced is_closed_and_synced;

  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  ledger::PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(
      fidl::MakeOptional(id), page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  storage_ptr->set_page_storage_synced(storage_page_id, true);
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id, callback::Capture(callback::SetWhenCalled(&called),
                                         &status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(PageClosedAndSynced::NO, is_closed_and_synced);

  // Close the page. PageIsClosedAndSynced should now be true.
  page.Unbind();
  RunLoopUntilIdle();

  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id, callback::Capture(callback::SetWhenCalled(&called),
                                         &status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(PageClosedAndSynced::YES, is_closed_and_synced);
}

// Check for a page that exists, is closed, but is not synced.
// PageIsClosedAndSynced should be false.
TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckSynced) {
  bool called;
  Status status;
  PageClosedAndSynced is_closed_and_synced;

  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  ledger::PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(
      fidl::MakeOptional(id), page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);

  // Mark the page as unsynced and close it.
  storage_ptr->set_page_storage_synced(storage_page_id, false);
  page.Unbind();

  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id, callback::Capture(callback::SetWhenCalled(&called),
                                         &status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(PageClosedAndSynced::NO, is_closed_and_synced);
}

// Check for a page that exists, is closed, and synced, but was opened during
// the PageIsClosedAndSynced call. Expect an |UNKNOWN| return status.
TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckUnknown) {
  bool called;
  Status status;
  PageClosedAndSynced is_closed_and_synced;

  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  ledger::PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(
      fidl::MakeOptional(id), page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  // Mark the page as synced and close it.
  storage_ptr->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();

  // Call PageIsClosedAndSynced but don't let it terminate.
  bool page_is_closed_and_synced_called = false;
  storage_ptr->DelayIsSyncedCallback(storage_page_id, true);
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id, callback::Capture(callback::SetWhenCalled(
                                             &page_is_closed_and_synced_called),
                                         &status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_FALSE(page_is_closed_and_synced_called);

  // Open and close the page.
  ledger_->GetPage(
      fidl::MakeOptional(id), page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  page.Unbind();
  RunLoopUntilIdle();

  // Make sure PageIsClosedAndSynced terminates with a |UNKNOWN| status.
  storage_ptr->CallIsSyncedCallback(storage_page_id);
  EXPECT_TRUE(page_is_closed_and_synced_called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_EQ(PageClosedAndSynced::UNKNOWN, is_closed_and_synced);
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
