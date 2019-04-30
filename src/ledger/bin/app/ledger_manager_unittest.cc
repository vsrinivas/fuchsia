// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/callback/waiter.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/inspect/inspect.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/public/ledger_storage.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace ledger {
namespace {

constexpr char kLedgerName[] = "ledger_under_test";

class DelayingCallbacksManager {
 public:
  DelayingCallbacksManager() {}
  virtual ~DelayingCallbacksManager() {}

  // Returns true if the PageStorage of the page with the given id should delay
  // calling the callback of |IsSynced|.
  virtual bool ShouldDelayIsSyncedCallback(storage::PageIdView page_id) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DelayingCallbacksManager);
};

class DelayIsSyncedCallbackFakePageStorage
    : public storage::fake::FakePageStorage {
 public:
  explicit DelayIsSyncedCallbackFakePageStorage(
      Environment* environment,
      DelayingCallbacksManager* delaying_callbacks_manager, storage::PageId id)
      : storage::fake::FakePageStorage(environment, id),
        delaying_callbacks_manager_(delaying_callbacks_manager) {}
  ~DelayIsSyncedCallbackFakePageStorage() override {}

  void IsSynced(fit::function<void(storage::Status, bool)> callback) override {
    if (!delaying_callbacks_manager_->ShouldDelayIsSyncedCallback(page_id_)) {
      storage::fake::FakePageStorage::IsSynced(std::move(callback));
      return;
    }
    is_synced_callback_ = std::move(callback);
  }

  void IsEmpty(fit::function<void(storage::Status, bool)> callback) override {
    callback(storage::Status::OK, true);
  }

  bool IsOnline() override { return false; }

  void CallIsSyncedCallback() {
    storage::fake::FakePageStorage::IsSynced(std::move(is_synced_callback_));
  }

 private:
  fit::function<void(storage::Status, bool)> is_synced_callback_;
  DelayingCallbacksManager* delaying_callbacks_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DelayIsSyncedCallbackFakePageStorage);
};

class FakeLedgerStorage : public storage::LedgerStorage,
                          public DelayingCallbacksManager {
 public:
  explicit FakeLedgerStorage(Environment* environment)
      : environment_(environment) {}
  ~FakeLedgerStorage() override {}

  void CreatePageStorage(
      storage::PageId page_id,
      fit::function<void(storage::Status,
                         std::unique_ptr<storage::PageStorage>)>
          callback) override {
    create_page_calls.push_back(std::move(page_id));
    callback(storage::Status::IO_ERROR, nullptr);
  }

  void GetPageStorage(storage::PageId page_id,
                      fit::function<void(storage::Status,
                                         std::unique_ptr<storage::PageStorage>)>
                          callback) override {
    get_page_calls.push_back(page_id);
    async::PostTask(
        environment_->dispatcher(),
        [this, callback = std::move(callback), page_id]() mutable {
          if (should_get_page_fail) {
            callback(storage::Status::PAGE_NOT_FOUND, nullptr);
          } else {
            auto fake_page_storage =
                std::make_unique<DelayIsSyncedCallbackFakePageStorage>(
                    environment_, this, page_id);
            // If the page was opened before, restore the previous sync state.
            fake_page_storage->set_synced(synced_pages_.find(page_id) !=
                                          synced_pages_.end());
            page_storages_[std::move(page_id)] = fake_page_storage.get();
            callback(storage::Status::OK, std::move(fake_page_storage));
          }
        });
  }

  void DeletePageStorage(
      storage::PageIdView /*page_id*/,
      fit::function<void(storage::Status)> callback) override {
    delete_page_storage_callback = std::move(callback);
  }

  void ClearCalls() {
    create_page_calls.clear();
    get_page_calls.clear();
    page_storages_.clear();
  }

  void DelayIsSyncedCallback(storage::PageIdView page_id, bool delay_callback) {
    if (delay_callback) {
      pages_with_delayed_callback.insert(page_id.ToString());
    } else {
      pages_with_delayed_callback.erase(page_id.ToString());
    }
  }

  // DelayingCallbacksManager:
  bool ShouldDelayIsSyncedCallback(storage::PageIdView page_id) override {
    return pages_with_delayed_callback.find(page_id.ToString()) !=
           pages_with_delayed_callback.end();
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
    page_storages_[page_id_string]->set_synced(is_synced);
  }

  void set_page_storage_offline_empty(storage::PageIdView page_id,
                                      bool is_offline_empty) {
    storage::PageId page_id_string = page_id.ToString();
    if (is_offline_empty) {
      offline_empty_pages_.insert(page_id_string);
    } else {
      auto it = offline_empty_pages_.find(page_id_string);
      if (it != offline_empty_pages_.end()) {
        offline_empty_pages_.erase(it);
      }
    }
  }

  bool should_get_page_fail = false;
  std::vector<storage::PageId> create_page_calls;
  std::vector<storage::PageId> get_page_calls;
  fit::function<void(storage::Status)> delete_page_storage_callback;

 private:
  Environment* const environment_;
  std::map<storage::PageId, DelayIsSyncedCallbackFakePageStorage*>
      page_storages_;
  std::set<storage::PageId> synced_pages_;
  std::set<storage::PageId> offline_empty_pages_;
  std::set<storage::PageId> pages_with_delayed_callback;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerStorage);
};

class FakeLedgerSync : public sync_coordinator::LedgerSync {
 public:
  FakeLedgerSync() {}
  ~FakeLedgerSync() override {}

  std::unique_ptr<sync_coordinator::PageSync> CreatePageSync(
      storage::PageStorage* /*page_storage*/,
      storage::PageSyncClient* /*page_sync_client*/) override {
    called = true;
    return nullptr;
  }

  bool called = false;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLedgerSync);
};

class LedgerManagerTest : public TestWithEnvironment {
 public:
  LedgerManagerTest() {}

  ~LedgerManagerTest() override {}

  // gtest::TestWithEnvironment:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    std::unique_ptr<FakeLedgerStorage> storage =
        std::make_unique<FakeLedgerStorage>(&environment_);
    storage_ptr = storage.get();
    std::unique_ptr<FakeLedgerSync> sync = std::make_unique<FakeLedgerSync>();
    sync_ptr = sync.get();
    disk_cleanup_manager_ = std::make_unique<FakeDiskCleanupManager>();
    ledger_manager_ = std::make_unique<LedgerManager>(
        &environment_, kLedgerName, inspect::Object(),
        std::make_unique<encryption::FakeEncryptionService>(dispatcher()),
        std::move(storage), std::move(sync), disk_cleanup_manager_.get());
    ResetLedgerPtr();
  }

  void ResetLedgerPtr() { ledger_manager_->BindLedger(ledger_.NewRequest()); }

  PageId RandomId() {
    PageId result;
    environment_.random()->Draw(&result.id);
    return result;
  }

 protected:
  FakeLedgerStorage* storage_ptr;
  FakeLedgerSync* sync_ptr;
  std::unique_ptr<FakeDiskCleanupManager> disk_cleanup_manager_;
  std::unique_ptr<LedgerManager> ledger_manager_;
  LedgerPtr ledger_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerManagerTest);
};

class StubConflictResolverFactory : public ConflictResolverFactory {
 public:
  explicit StubConflictResolverFactory(
      fidl::InterfaceRequest<ConflictResolverFactory> request)
      : binding_(this, std::move(request)) {
    binding_.set_error_handler(
        [this](zx_status_t status) { disconnected = true; });
  }

  bool disconnected = false;

 private:
  void GetPolicy(PageId page_id,
                 fit::function<void(MergePolicy)> callback) override {}

  void NewConflictResolver(
      PageId page_id,
      fidl::InterfaceRequest<ConflictResolver> resolver) override {}

  fidl::Binding<ConflictResolverFactory> binding_;
};

// Verifies that LedgerImpl proxies vended by LedgerManager work correctly,
// that is, make correct calls to ledger storage.
TEST_F(LedgerManagerTest, LedgerImpl) {
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(0u, storage_ptr->get_page_calls.size());

  PagePtr page;
  storage_ptr->should_get_page_fail = true;
  ledger_->GetPage(nullptr, page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_FALSE(ledger_);
  page.Unbind();
  storage_ptr->ClearCalls();

  ResetLedgerPtr();
  storage_ptr->should_get_page_fail = true;
  ledger_->GetRootPage(page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  EXPECT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_FALSE(ledger_);
  page.Unbind();
  storage_ptr->ClearCalls();

  ResetLedgerPtr();
  PageId id = RandomId();
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(1u, storage_ptr->create_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_FALSE(ledger_);
  EXPECT_EQ(convert::ToString(id.id), storage_ptr->get_page_calls[0]);
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

TEST_F(LedgerManagerTest, OnEmptyCalled) {
  bool on_empty_called;
  ledger_manager_->set_on_empty(callback::SetWhenCalled(&on_empty_called));

  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

// Verifies that the LedgerManager does not call its callback while a page is
// being deleted.
TEST_F(LedgerManagerTest, NonEmptyDuringDeletion) {
  bool on_empty_called;
  ledger_manager_->set_on_empty(callback::SetWhenCalled(&on_empty_called));

  PageId id = RandomId();
  bool delete_page_called;
  storage::Status delete_page_status;
  ledger_manager_->DeletePageStorage(
      id.id, callback::Capture(callback::SetWhenCalled(&delete_page_called),
                               &delete_page_status));

  // Empty the Ledger manager.
  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);

  // Complete the deletion successfully.
  ASSERT_TRUE(storage_ptr->delete_page_storage_callback);
  storage_ptr->delete_page_storage_callback(storage::Status::OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(delete_page_called);
  EXPECT_EQ(storage::Status::OK, delete_page_status);
  EXPECT_TRUE(on_empty_called);
}

TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckNotFound) {
  bool called;
  storage::Status status;
  PagePredicateResult is_closed_and_synced;

  PageId id = RandomId();

  // Check for a page that doesn't exist.
  storage_ptr->should_get_page_fail = true;
  ledger_manager_->PageIsClosedAndSynced(
      id.id, callback::Capture(callback::SetWhenCalled(&called), &status,
                               &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::PAGE_NOT_FOUND, status);
}

// Check for a page that exists, is synced and open. PageIsClosedAndSynced
// should be false.
TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckClosed) {
  bool called;
  PagePredicateResult is_closed_and_synced;

  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();

  storage::Status storage_status;
  storage_ptr->set_page_storage_synced(storage_page_id, true);
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id,
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_and_synced);

  // Close the page. PageIsClosedAndSynced should now be true.
  page.Unbind();
  RunLoopUntilIdle();

  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id,
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_and_synced);
}

// Check for a page that exists, is closed, but is not synced.
// PageIsClosedAndSynced should be false.
TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckSynced) {
  bool called;
  PagePredicateResult is_closed_and_synced;

  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();

  // Mark the page as unsynced and close it.
  storage_ptr->set_page_storage_synced(storage_page_id, false);
  page.Unbind();
  RunLoopUntilIdle();

  storage::Status storage_status;
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id,
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::NO, is_closed_and_synced);
}

// Check for a page that exists, is closed, and synced, but was opened during
// the PageIsClosedAndSynced call. Expect an |PAGE_OPENED| result.
TEST_F(LedgerManagerTest, PageIsClosedAndSyncedCheckPageOpened) {
  PagePredicateResult is_closed_and_synced;

  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  // Mark the page as synced and close it.
  storage_ptr->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();

  // Call PageIsClosedAndSynced but don't let it terminate.
  bool page_is_closed_and_synced_called = false;
  storage_ptr->DelayIsSyncedCallback(storage_page_id, true);
  storage::Status storage_status;
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id,
      callback::Capture(
          callback::SetWhenCalled(&page_is_closed_and_synced_called),
          &storage_status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_FALSE(page_is_closed_and_synced_called);

  // Open and close the page.
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  page.Unbind();
  RunLoopUntilIdle();

  // Make sure PageIsClosedAndSynced terminates with a |PAGE_OPENED| result.
  storage_ptr->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_TRUE(page_is_closed_and_synced_called);
  EXPECT_EQ(storage::Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_and_synced);
}

// Check for a page that exists, is closed, and synced. Test two concurrent
// calls to PageIsClosedAndSynced, where the second one will start and terminate
// without the page being opened by external requests.
TEST_F(LedgerManagerTest, PageIsClosedAndSyncedConcurrentCalls) {
  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  // Mark the page as synced and close it.
  storage_ptr->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();

  // Make a first call to PageIsClosedAndSynced but don't let it terminate.
  bool called1 = false;
  storage::Status status1;
  PagePredicateResult is_closed_and_synced1;
  storage_ptr->DelayIsSyncedCallback(storage_page_id, true);
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id, callback::Capture(callback::SetWhenCalled(&called1),
                                         &status1, &is_closed_and_synced1));
  RunLoopUntilIdle();

  // Prepare for the second call: it will return immediately and the expected
  // result is |YES|.
  bool called2 = false;
  storage::Status status2;
  PagePredicateResult is_closed_and_synced2;
  storage_ptr->DelayIsSyncedCallback(storage_page_id, false);
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id, callback::Capture(callback::SetWhenCalled(&called2),
                                         &status2, &is_closed_and_synced2));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);
  EXPECT_TRUE(called2);
  EXPECT_EQ(storage::Status::OK, status2);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_and_synced2);

  // Open and close the page.
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  page.Unbind();
  RunLoopUntilIdle();

  // Call the callback and let the first call to PageIsClosedAndSynced
  // terminate. The expected returned result is |PAGE_OPENED|.
  storage_ptr->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_TRUE(called1);
  EXPECT_EQ(storage::Status::OK, status1);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_and_synced1);
}

TEST_F(LedgerManagerTest, PageIsClosedOfflineAndEmptyCheckNotFound) {
  bool called;
  storage::Status status;
  PagePredicateResult is_closed_offline_empty;

  PageId id = RandomId();

  // Check for a page that doesn't exist.
  storage_ptr->should_get_page_fail = true;
  ledger_manager_->PageIsClosedOfflineAndEmpty(
      id.id, callback::Capture(callback::SetWhenCalled(&called), &status,
                               &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::PAGE_NOT_FOUND, status);
}

TEST_F(LedgerManagerTest, PageIsClosedOfflineAndEmptyCheckClosed) {
  bool called;
  PagePredicateResult is_closed_offline_empty;

  storage_ptr->should_get_page_fail = false;
  PagePtr page;
  PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();

  storage_ptr->set_page_storage_offline_empty(storage_page_id, true);
  storage::Status storage_status;
  ledger_manager_->PageIsClosedOfflineAndEmpty(
      storage_page_id,
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_offline_empty);

  // Close the page. PagePredicateResult should now be true.
  page.Unbind();
  RunLoopUntilIdle();

  ledger_manager_->PageIsClosedOfflineAndEmpty(
      storage_page_id,
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_offline_empty);
}

TEST_F(LedgerManagerTest, PageIsClosedOfflineAndEmptyCanDeletePageOnCallback) {
  bool page_is_empty_called = false;
  storage::Status page_is_empty_status;
  PagePredicateResult is_closed_offline_empty;
  bool delete_page_called = false;
  storage::Status delete_page_status;
  PageId id = RandomId();

  // The page is closed, offline and empty. Try to delete the page storage in
  // the callback.
  storage_ptr->set_page_storage_offline_empty(id.id, true);
  ledger_manager_->PageIsClosedOfflineAndEmpty(
      id.id, [&](storage::Status status, PagePredicateResult result) {
        page_is_empty_called = true;
        page_is_empty_status = status;
        is_closed_offline_empty = result;

        ledger_manager_->DeletePageStorage(
            id.id,
            callback::Capture(callback::SetWhenCalled(&delete_page_called),
                              &delete_page_status));
      });
  RunLoopUntilIdle();
  // Make sure the deletion finishes successfully.
  ASSERT_NE(nullptr, storage_ptr->delete_page_storage_callback);
  storage_ptr->delete_page_storage_callback(storage::Status::OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(page_is_empty_called);
  EXPECT_EQ(storage::Status::OK, page_is_empty_status);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_offline_empty);

  EXPECT_TRUE(delete_page_called);
  EXPECT_EQ(storage::Status::OK, delete_page_status);
}

// Verifies that two successive calls to GetPage do not create 2 storages.
TEST_F(LedgerManagerTest, CallGetPageTwice) {
  PageId id = RandomId();

  PagePtr page1;
  ledger_->GetPage(fidl::MakeOptional(id), page1.NewRequest());
  PagePtr page2;
  ledger_->GetPage(fidl::MakeOptional(id), page2.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(0u, storage_ptr->create_page_calls.size());
  ASSERT_EQ(1u, storage_ptr->get_page_calls.size());
  EXPECT_EQ(convert::ToString(id.id), storage_ptr->get_page_calls[0]);
}

// Cloud should never be queried.
TEST_F(LedgerManagerTest, GetPageDoNotCallTheCloud) {
  storage_ptr->should_get_page_fail = true;
  zx_status_t status;

  PagePtr page;
  PageId id = RandomId();
  bool called;
  storage_ptr->ClearCalls();
  // Get the root page.
  ledger_.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  ledger_->GetRootPage(page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(ledger_.is_bound());
  EXPECT_TRUE(called);
  EXPECT_EQ(static_cast<int32_t>(storage::Status::INTERNAL_ERROR),
            static_cast<int32_t>(status));
  EXPECT_FALSE(ledger_);
  EXPECT_FALSE(sync_ptr->called);

  page.Unbind();
  ResetLedgerPtr();
  storage_ptr->ClearCalls();

  // Get a new page with a random id.
  ledger_.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(ledger_.is_bound());
  EXPECT_TRUE(called);
  EXPECT_EQ(static_cast<int32_t>(storage::Status::INTERNAL_ERROR),
            static_cast<int32_t>(status));
  EXPECT_FALSE(ledger_);
  EXPECT_FALSE(sync_ptr->called);

  page.Unbind();
  ResetLedgerPtr();
  storage_ptr->ClearCalls();

  // Create a new page.
  ledger_.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&called), &status));
  ledger_->GetPage(nullptr, page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_FALSE(ledger_.is_bound());
  EXPECT_TRUE(called);
  EXPECT_EQ(static_cast<int32_t>(storage::Status::INTERNAL_ERROR),
            static_cast<int32_t>(status));
  EXPECT_FALSE(ledger_);
  EXPECT_FALSE(sync_ptr->called);
}

TEST_F(LedgerManagerTest, OnPageOpenedClosedCalls) {
  PagePtr page1;
  PagePtr page2;
  PageId id = RandomId();

  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open a page and check that OnPageOpened was called once.
  ledger_->GetPage(fidl::MakeOptional(id), page1.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open the page again and check that there is no new call to OnPageOpened.
  ledger_->GetPage(fidl::MakeOptional(id), page2.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Close one of the two connections and check that there is still no call to
  // OnPageClosed.
  page1.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Close the second connection and check that OnPageClosed was called once.
  page2.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_unused_count);
}

TEST_F(LedgerManagerTest, OnPageOpenedClosedCallInternalRequest) {
  PagePtr page;
  PageId id = RandomId();

  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Make an internal request by calling PageIsClosedAndSynced. No calls to page
  // opened/closed should be made.
  bool called;
  storage::Status storage_status;
  PagePredicateResult page_state;
  ledger_manager_->PageIsClosedAndSynced(
      convert::ToString(id.id),
      callback::Capture(callback::SetWhenCalled(&called), &storage_status,
                        &page_state));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::NO, page_state);
  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open the same page with an external request and check that OnPageOpened
  // was called once.
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);
}

TEST_F(LedgerManagerTest, OnPageOpenedClosedUnused) {
  PagePtr page;
  PageId id = RandomId();
  storage::PageIdView storage_page_id = convert::ExtendedStringView(id.id);

  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open and close the page through an external request.
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  // Mark the page as synced and close it.
  storage_ptr->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_unused_count);

  // Start an internal request but don't let it terminate. Nothing should have
  // changed in the notifications received.
  PagePredicateResult is_synced;
  bool page_is_synced_called = false;
  storage_ptr->DelayIsSyncedCallback(storage_page_id, true);
  storage::Status storage_status;
  ledger_manager_->PageIsClosedAndSynced(
      storage_page_id,
      callback::Capture(callback::SetWhenCalled(&page_is_synced_called),
                        &storage_status, &is_synced));
  RunLoopUntilIdle();
  EXPECT_FALSE(page_is_synced_called);
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_unused_count);

  // Open the same page with an external request and check that OnPageOpened
  // was called once.
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();
  EXPECT_EQ(2, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_unused_count);

  // Close the page. We should get the page closed notification, but not the
  // unused one: the internal request is still running.
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(2, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(2, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_unused_count);

  // Terminate the internal request. We should now see the unused page
  // notification.
  storage_ptr->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_EQ(2, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(2, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(2, disk_cleanup_manager_->page_unused_count);
}

TEST_F(LedgerManagerTest, DeletePageStorageWhenPageOpenFails) {
  PagePtr page;
  PageId id = RandomId();
  bool called;

  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  RunLoopUntilIdle();

  // Try to delete the page while it is open. Expect to get an error.
  storage::Status storage_status;
  ledger_manager_->DeletePageStorage(
      id.id,
      callback::Capture(callback::SetWhenCalled(&called), &storage_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(storage::Status::ILLEGAL_STATE, storage_status);
}

TEST_F(LedgerManagerTest, OpenPageWithDeletePageStorageInProgress) {
  PagePtr page;
  PageId id = RandomId();

  // Start deleting the page.
  bool delete_called;
  storage::Status delete_status;
  ledger_manager_->DeletePageStorage(
      id.id, callback::Capture(callback::SetWhenCalled(&delete_called),
                               &delete_status));
  RunLoopUntilIdle();
  EXPECT_FALSE(delete_called);

  // Try to open the same page.
  bool get_page_done;
  ledger_->GetPage(fidl::MakeOptional(id), page.NewRequest());
  ledger_->Sync(callback::SetWhenCalled(&get_page_done));
  RunLoopUntilIdle();
  EXPECT_FALSE(get_page_done);

  // After calling the callback registered in |DeletePageStorage| both
  // operations should terminate without an error.
  storage_ptr->delete_page_storage_callback(storage::Status::OK);

  RunLoopUntilIdle();
  EXPECT_TRUE(delete_called);
  EXPECT_EQ(storage::Status::OK, delete_status);

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

}  // namespace
}  // namespace ledger
