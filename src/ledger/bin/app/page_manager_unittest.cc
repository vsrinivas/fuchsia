// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/callback/waiter.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <memory>
#include <set>
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

class DelayIsSyncedCallbackFakePageStorage : public storage::fake::FakePageStorage {
 public:
  explicit DelayIsSyncedCallbackFakePageStorage(
      Environment* environment, DelayingCallbacksManager* delaying_callbacks_manager,
      storage::PageId id)
      : storage::fake::FakePageStorage(environment, id),
        delaying_callbacks_manager_(delaying_callbacks_manager) {}
  ~DelayIsSyncedCallbackFakePageStorage() override {}

  void IsSynced(fit::function<void(Status, bool)> callback) override {
    if (!delaying_callbacks_manager_->ShouldDelayIsSyncedCallback(page_id_)) {
      storage::fake::FakePageStorage::IsSynced(std::move(callback));
      return;
    }
    is_synced_callback_ = std::move(callback);
  }

  void IsEmpty(fit::function<void(Status, bool)> callback) override { callback(Status::OK, true); }

  bool IsOnline() override { return false; }

  void CallIsSyncedCallback() {
    storage::fake::FakePageStorage::IsSynced(std::move(is_synced_callback_));
  }

 private:
  fit::function<void(Status, bool)> is_synced_callback_;
  DelayingCallbacksManager* delaying_callbacks_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DelayIsSyncedCallbackFakePageStorage);
};

class FakeLedgerStorage : public storage::LedgerStorage, public DelayingCallbacksManager {
 public:
  explicit FakeLedgerStorage(Environment* environment) : environment_(environment) {}
  ~FakeLedgerStorage() override {}

  void ListPages(
      fit::function<void(storage::Status, std::set<storage::PageId>)> callback) override {
    FXL_NOTREACHED() << "Maybe implement this later on if needed?";
  }

  void CreatePageStorage(
      storage::PageId page_id,
      fit::function<void(Status, std::unique_ptr<storage::PageStorage>)> callback) override {
    create_page_calls.push_back(std::move(page_id));
    callback(Status::IO_ERROR, nullptr);
  }

  void GetPageStorage(
      storage::PageId page_id,
      fit::function<void(Status, std::unique_ptr<storage::PageStorage>)> callback) override {
    get_page_calls.push_back(page_id);
    async::PostTask(
        environment_->dispatcher(), [this, callback = std::move(callback), page_id]() mutable {
          if (should_get_page_fail) {
            callback(Status::PAGE_NOT_FOUND, nullptr);
          } else {
            auto fake_page_storage =
                std::make_unique<DelayIsSyncedCallbackFakePageStorage>(environment_, this, page_id);
            // If the page was opened before, restore the previous sync state.
            fake_page_storage->set_synced(synced_pages_.find(page_id) != synced_pages_.end());
            page_storages_[std::move(page_id)] = fake_page_storage.get();
            callback(Status::OK, std::move(fake_page_storage));
          }
        });
  }

  void DeletePageStorage(storage::PageIdView /*page_id*/,
                         fit::function<void(Status)> callback) override {
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

  void set_page_storage_offline_empty(storage::PageIdView page_id, bool is_offline_empty) {
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
  fit::function<void(Status)> delete_page_storage_callback;

 private:
  Environment* const environment_;
  std::map<storage::PageId, DelayIsSyncedCallbackFakePageStorage*> page_storages_;
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

class PageManagerTest : public TestWithEnvironment {
 public:
  PageManagerTest() {}

  ~PageManagerTest() override {}

  // gtest::TestWithEnvironment:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    page_id_ = RandomId();
    ledger_merge_manager_ = std::make_unique<LedgerMergeManager>(&environment_);
    storage_ = std::make_unique<FakeLedgerStorage>(&environment_);
    sync_ = std::make_unique<FakeLedgerSync>();
    disk_cleanup_manager_ = std::make_unique<FakeDiskCleanupManager>();
    page_manager_ = std::make_unique<PageManager>(
        &environment_, kLedgerName, convert::ToString(page_id_.id), disk_cleanup_manager_.get(),
        storage_.get(), sync_.get(), ledger_merge_manager_.get(), inspect_deprecated::Node());
  }

  PageId RandomId() {
    PageId result;
    environment_.random()->Draw(&result.id);
    return result;
  }

 protected:
  std::unique_ptr<FakeLedgerStorage> storage_;
  std::unique_ptr<FakeLedgerSync> sync_;
  std::unique_ptr<LedgerMergeManager> ledger_merge_manager_;
  std::unique_ptr<FakeDiskCleanupManager> disk_cleanup_manager_;
  std::unique_ptr<PageManager> page_manager_;
  PageId page_id_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageManagerTest);
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

TEST_F(PageManagerTest, OnEmptyCalled) {
  bool get_page_callback_called;
  Status get_page_status;
  bool on_empty_callback_called;

  page_manager_->set_on_empty(
      callback::Capture(callback::SetWhenCalled(&on_empty_callback_called)));

  PagePtr page;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NEW, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_FALSE(on_empty_callback_called);

  fit::closure detacher = page_manager_->CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_callback_called);

  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_callback_called);

  detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_callback_called);
}

TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckNotFound) {
  bool called;
  Status status;
  PagePredicateResult is_closed_and_synced;

  // Check for a page that doesn't exist.
  storage_->should_get_page_fail = true;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PAGE_NOT_FOUND, status);
}

// Check for a page that exists, is synced and open. PageIsClosedAndSynced
// should be false.
TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckClosed) {
  bool get_page_callback_called;
  Status get_page_status;
  bool called;
  PagePredicateResult is_closed_and_synced;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);

  Status storage_status;
  storage_->set_page_storage_synced(storage_page_id, true);
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_and_synced);

  // Close the page. PageIsClosedAndSynced should now be true.
  page.Unbind();
  RunLoopUntilIdle();

  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_and_synced);
}

// Check for a page that exists, is closed, but is not synced.
// PageIsClosedAndSynced should be false.
TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckSynced) {
  bool get_page_callback_called;
  Status get_page_status;
  bool called;
  PagePredicateResult is_closed_and_synced;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);

  // Mark the page as unsynced and close it.
  storage_->set_page_storage_synced(storage_page_id, false);
  page.Unbind();
  RunLoopUntilIdle();

  Status storage_status;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::NO, is_closed_and_synced);
}

// Check for a page that exists, is closed, and synced, but was opened during
// the PageIsClosedAndSynced call. Expect an |PAGE_OPENED| result.
TEST_F(PageManagerTest, PageIsClosedAndSyncedCheckPageOpened) {
  bool get_page_callback_called;
  Status get_page_status;
  PagePredicateResult is_closed_and_synced;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);

  // Mark the page as synced and close it.
  storage_->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();

  // Call PageIsClosedAndSynced but don't let it terminate.
  bool page_is_closed_and_synced_called = false;
  storage_->DelayIsSyncedCallback(storage_page_id, true);
  Status storage_status;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&page_is_closed_and_synced_called), &storage_status,
                        &is_closed_and_synced));
  RunLoopUntilIdle();
  EXPECT_FALSE(page_is_closed_and_synced_called);

  // Open and close the page.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  page.Unbind();
  RunLoopUntilIdle();

  // Make sure PageIsClosedAndSynced terminates with a |PAGE_OPENED| result.
  storage_->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_TRUE(page_is_closed_and_synced_called);
  EXPECT_EQ(Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_and_synced);
}

// Check for a page that exists, is closed, and synced. Test two concurrent
// calls to PageIsClosedAndSynced, where the second one will start and terminate
// without the page being opened by external requests.
TEST_F(PageManagerTest, PageIsClosedAndSyncedConcurrentCalls) {
  bool get_page_callback_called;
  Status get_page_status;
  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);

  // Mark the page as synced and close it.
  storage_->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();

  // Make a first call to PageIsClosedAndSynced but don't let it terminate.
  bool called1 = false;
  Status status1;
  PagePredicateResult is_closed_and_synced1;
  storage_->DelayIsSyncedCallback(storage_page_id, true);
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called1), &status1, &is_closed_and_synced1));
  RunLoopUntilIdle();

  // Prepare for the second call: it will return immediately and the expected
  // result is |YES|.
  bool called2 = false;
  Status status2;
  PagePredicateResult is_closed_and_synced2;
  storage_->DelayIsSyncedCallback(storage_page_id, false);
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called2), &status2, &is_closed_and_synced2));
  RunLoopUntilIdle();
  EXPECT_FALSE(called1);
  EXPECT_TRUE(called2);
  EXPECT_EQ(Status::OK, status2);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_and_synced2);

  // Open and close the page.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  page.Unbind();
  RunLoopUntilIdle();

  // Call the callback and let the first call to PageIsClosedAndSynced
  // terminate. The expected returned result is |PAGE_OPENED|.
  storage_->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_TRUE(called1);
  EXPECT_EQ(Status::OK, status1);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_and_synced1);
}

TEST_F(PageManagerTest, PageIsClosedOfflineAndEmptyCheckNotFound) {
  bool called;
  Status status;
  PagePredicateResult is_closed_offline_empty;

  // Check for a page that doesn't exist.
  storage_->should_get_page_fail = true;
  page_manager_->PageIsClosedOfflineAndEmpty(
      callback::Capture(callback::SetWhenCalled(&called), &status, &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::PAGE_NOT_FOUND, status);
}

TEST_F(PageManagerTest, PageIsClosedOfflineAndEmptyCheckClosed) {
  bool get_page_callback_called;
  Status get_page_status;
  bool called;
  PagePredicateResult is_closed_offline_empty;

  storage_->should_get_page_fail = false;
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);

  storage_->set_page_storage_offline_empty(storage_page_id, true);
  Status storage_status;
  page_manager_->PageIsClosedOfflineAndEmpty(callback::Capture(
      callback::SetWhenCalled(&called), &storage_status, &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::PAGE_OPENED, is_closed_offline_empty);

  // Close the page. PagePredicateResult should now be true.
  page.Unbind();
  RunLoopUntilIdle();

  page_manager_->PageIsClosedOfflineAndEmpty(callback::Capture(
      callback::SetWhenCalled(&called), &storage_status, &is_closed_offline_empty));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_offline_empty);
}

TEST_F(PageManagerTest, PageIsClosedOfflineAndEmptyCanDeletePageOnCallback) {
  bool page_is_empty_called = false;
  Status page_is_empty_status;
  PagePredicateResult is_closed_offline_empty;
  bool delete_page_called = false;
  Status delete_page_status;

  // The page is closed, offline and empty. Try to delete the page storage in
  // the callback.
  storage_->set_page_storage_offline_empty(page_id_.id, true);
  page_manager_->PageIsClosedOfflineAndEmpty([&](Status status, PagePredicateResult result) {
    page_is_empty_called = true;
    page_is_empty_status = status;
    is_closed_offline_empty = result;

    page_manager_->DeletePageStorage(
        callback::Capture(callback::SetWhenCalled(&delete_page_called), &delete_page_status));
  });
  RunLoopUntilIdle();
  // Make sure the deletion finishes successfully.
  ASSERT_NE(nullptr, storage_->delete_page_storage_callback);
  storage_->delete_page_storage_callback(Status::OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(page_is_empty_called);
  EXPECT_EQ(Status::OK, page_is_empty_status);
  EXPECT_EQ(PagePredicateResult::YES, is_closed_offline_empty);

  EXPECT_TRUE(delete_page_called);
  EXPECT_EQ(Status::OK, delete_page_status);
}

// Verifies that two successive calls to GetPage do not create 2 storages.
TEST_F(PageManagerTest, CallGetPageTwice) {
  PagePtr page1;
  bool get_page_callback_called1;
  Status get_page_status1;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page1.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called1), &get_page_status1));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called1);
  EXPECT_EQ(Status::OK, get_page_status1);
  PagePtr page2;
  bool get_page_callback_called2;
  Status get_page_status2;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page2.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called2), &get_page_status2));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called2);
  EXPECT_EQ(Status::OK, get_page_status2);
  EXPECT_EQ(0u, storage_->create_page_calls.size());
  ASSERT_EQ(1u, storage_->get_page_calls.size());
  EXPECT_EQ(convert::ToString(page_id_.id), storage_->get_page_calls[0]);
}

TEST_F(PageManagerTest, OnPageOpenedClosedCalls) {
  PagePtr page1;
  bool get_page_callback_called1;
  Status get_page_status1;
  PagePtr page2;
  bool get_page_callback_called2;
  Status get_page_status2;

  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open a page and check that OnPageOpened was called once.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page1.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called1), &get_page_status1));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called1);
  EXPECT_EQ(Status::OK, get_page_status1);
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open the page again and check that there is no new call to OnPageOpened.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page2.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called2), &get_page_status2));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called2);
  EXPECT_EQ(Status::OK, get_page_status2);
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

TEST_F(PageManagerTest, OnPageOpenedClosedCallInternalRequest) {
  PagePtr page;

  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Make an internal request by calling PageIsClosedAndSynced. No calls to page
  // opened/closed should be made.
  bool called;
  Status storage_status;
  PagePredicateResult page_state;
  page_manager_->PageIsClosedAndSynced(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status, &page_state));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, storage_status);
  EXPECT_EQ(PagePredicateResult::NO, page_state);
  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open the same page with an external request and check that OnPageOpened
  // was called once.
  bool get_page_callback_called;
  Status get_page_status;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);
}

TEST_F(PageManagerTest, OnPageOpenedClosedUnused) {
  PagePtr page;
  storage::PageIdView storage_page_id = convert::ExtendedStringView(page_id_.id);

  EXPECT_EQ(0, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(0, disk_cleanup_manager_->page_unused_count);

  // Open and close the page through an external request.
  bool get_page_callback_called;
  Status get_page_status;
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
  // Mark the page as synced and close it.
  storage_->set_page_storage_synced(storage_page_id, true);
  page.Unbind();
  RunLoopUntilIdle();
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_unused_count);

  // Start an internal request but don't let it terminate. Nothing should have
  // changed in the notifications received.
  PagePredicateResult is_synced;
  bool page_is_synced_called = false;
  storage_->DelayIsSyncedCallback(storage_page_id, true);
  Status storage_status;
  page_manager_->PageIsClosedAndSynced(callback::Capture(
      callback::SetWhenCalled(&page_is_synced_called), &storage_status, &is_synced));
  RunLoopUntilIdle();
  EXPECT_FALSE(page_is_synced_called);
  EXPECT_EQ(1, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(1, disk_cleanup_manager_->page_unused_count);

  // Open the same page with an external request and check that OnPageOpened
  // was called once.
  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);
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
  storage_->CallIsSyncedCallback(storage_page_id);
  RunLoopUntilIdle();

  EXPECT_EQ(2, disk_cleanup_manager_->page_opened_count);
  EXPECT_EQ(2, disk_cleanup_manager_->page_closed_count);
  EXPECT_EQ(2, disk_cleanup_manager_->page_unused_count);
}

TEST_F(PageManagerTest, DeletePageStorageWhenPageOpenFails) {
  bool get_page_callback_called;
  Status get_page_status;
  PagePtr page;
  bool called;

  page_manager_->GetPage(
      LedgerImpl::Delegate::PageState::NAMED, page.NewRequest(),
      callback::Capture(callback::SetWhenCalled(&get_page_callback_called), &get_page_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(get_page_callback_called);
  EXPECT_EQ(Status::OK, get_page_status);

  // Try to delete the page while it is open. Expect to get an error.
  Status storage_status;
  page_manager_->DeletePageStorage(
      callback::Capture(callback::SetWhenCalled(&called), &storage_status));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::ILLEGAL_STATE, storage_status);
}

}  // namespace
}  // namespace ledger
