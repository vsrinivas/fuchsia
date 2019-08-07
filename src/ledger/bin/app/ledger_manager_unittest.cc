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
#include <lib/inspect_deprecated/inspect.h>
#include <lib/inspect_deprecated/testing/inspect.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/fake/fake_db_factory.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"
#include "src/ledger/bin/storage/public/ledger_storage.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/bin/sync_coordinator/testing/fake_ledger_sync.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {
namespace {

using ::inspect_deprecated::testing::ChildrenMatch;
using ::inspect_deprecated::testing::NameMatches;
using ::inspect_deprecated::testing::NodeMatches;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

constexpr fxl::StringView kLedgerName = "ledger_under_test";
constexpr fxl::StringView kTestTopLevelNodeName = "top-level-of-test node";
constexpr fxl::StringView kInspectPathComponent = "test_ledger";

PageId RandomId(const Environment& environment) {
  PageId result;
  environment.random()->Draw(&result.id);
  return result;
}

PageId SpecificId(const storage::PageId& page_id) {
  PageId fidl_page_id;
  convert::ToArray(page_id, &fidl_page_id.id);
  return fidl_page_id;
}

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

class LedgerManagerTest : public TestWithEnvironment {
 public:
  LedgerManagerTest() {}

  ~LedgerManagerTest() override {}

  // gtest::TestWithEnvironment:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    std::unique_ptr<FakeLedgerStorage> storage = std::make_unique<FakeLedgerStorage>(&environment_);
    storage_ptr = storage.get();
    std::unique_ptr<sync_coordinator::FakeLedgerSync> sync =
        std::make_unique<sync_coordinator::FakeLedgerSync>();
    sync_ptr = sync.get();
    top_level_node_ = inspect_deprecated::Node(kTestTopLevelNodeName.ToString());
    disk_cleanup_manager_ = std::make_unique<FakeDiskCleanupManager>();
    ledger_manager_ = std::make_unique<LedgerManager>(
        &environment_, kLedgerName.ToString(),
        top_level_node_.CreateChild(kInspectPathComponent.ToString()),
        std::make_unique<encryption::FakeEncryptionService>(dispatcher()), std::move(storage),
        std::move(sync), std::vector<PageUsageListener*>{disk_cleanup_manager_.get()});
    ResetLedgerPtr();
  }

  void ResetLedgerPtr() { ledger_manager_->BindLedger(ledger_.NewRequest()); }

 protected:
  FakeLedgerStorage* storage_ptr;
  sync_coordinator::FakeLedgerSync* sync_ptr;
  inspect_deprecated::Node top_level_node_;
  std::unique_ptr<FakeDiskCleanupManager> disk_cleanup_manager_;
  std::unique_ptr<LedgerManager> ledger_manager_;
  LedgerPtr ledger_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerManagerTest);
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

TEST_F(LedgerManagerTest, OnEmptyCalled) {
  bool on_empty_called;
  ledger_manager_->set_on_empty(callback::SetWhenCalled(&on_empty_called));

  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(LedgerManagerTest, OnEmptyCalledWhenLastDetacherCalled) {
  bool on_empty_called;
  ledger_manager_->set_on_empty(callback::SetWhenCalled(&on_empty_called));
  auto first_detacher = ledger_manager_->CreateDetacher();
  auto second_detacher = ledger_manager_->CreateDetacher();

  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);

  first_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);

  auto third_detacher = ledger_manager_->CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);

  second_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);

  third_detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

// Verifies that the LedgerManager does not call its callback while a page is
// being deleted.
TEST_F(LedgerManagerTest, NonEmptyDuringDeletion) {
  bool on_empty_called;
  ledger_manager_->set_on_empty(callback::SetWhenCalled(&on_empty_called));

  PageId id = RandomId(environment_);
  bool delete_page_called;
  Status delete_page_status;
  ledger_manager_->DeletePageStorage(
      id.id, callback::Capture(callback::SetWhenCalled(&delete_page_called), &delete_page_status));

  // Empty the Ledger manager.
  ledger_.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_empty_called);

  // Complete the deletion successfully.
  ASSERT_TRUE(storage_ptr->delete_page_storage_callback);
  storage_ptr->delete_page_storage_callback(Status::OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(delete_page_called);
  EXPECT_EQ(delete_page_status, Status::OK);
  EXPECT_TRUE(on_empty_called);
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
  ledger_.set_error_handler(callback::Capture(callback::SetWhenCalled(&called), &status));
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
  ledger_.set_error_handler(callback::Capture(callback::SetWhenCalled(&called), &status));
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
  ledger_.set_error_handler(callback::Capture(callback::SetWhenCalled(&called), &status));
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
  ledger_manager_->DeletePageStorage(
      id.id, callback::Capture(callback::SetWhenCalled(&delete_called), &delete_status));
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
  storage::PageId page_id = convert::ExtendedStringView(id.id).ToString();

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
  storage::PageId page_id = convert::ExtendedStringView(id.id).ToString();

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
  DelayingLedgerStorage() {}
  ~DelayingLedgerStorage() override {}

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
    FXL_NOTIMPLEMENTED();
    callback(Status::NOT_IMPLEMENTED);
  }

  void ListPages(
      fit::function<void(storage::Status, std::set<storage::PageId>)> callback) override {
    FXL_NOTIMPLEMENTED();
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
      &environment_, kLedgerName.ToString(),
      top_level_node_.CreateChild(kInspectPathComponent.ToString()),
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

// Constructs a Matcher to be matched against a test-owned Inspect object (the
// Inspect object to which the LedgerManager under test attaches a child) that
// validates that the matched object has a hierarchy with a node for the
// LedgerManager under test, a node named |kPagesInspectPathComponent|
// under that, and a node for each of the given |page_ids| under that.
testing::Matcher<const inspect_deprecated::ObjectHierarchy&> HierarchyMatcher(
    const std::vector<storage::PageId>& page_ids) {
  auto page_expectations =
      std::vector<testing::Matcher<const inspect_deprecated::ObjectHierarchy&>>();
  std::set<storage::PageId> sorted_and_unique_page_ids;
  for (const storage::PageId& page_id : page_ids) {
    sorted_and_unique_page_ids.insert(page_id);
  }
  for (const storage::PageId& page_id : sorted_and_unique_page_ids) {
    page_expectations.push_back(NodeMatches(NameMatches(PageIdToDisplayName(page_id))));
  }
  return ChildrenMatch(ElementsAre(ChildrenMatch(
      ElementsAre(AllOf(NodeMatches(NameMatches(kPagesInspectPathComponent.ToString())),
                        ChildrenMatch(ElementsAreArray(page_expectations)))))));
}

// TODO(LE-800): Make FakeLedgerStorage usable as a real LedgerStorage and unify this class with
// LedgerManagerTest.
class LedgerManagerWithRealStorageTest : public TestWithEnvironment {
 public:
  LedgerManagerWithRealStorageTest() = default;
  ~LedgerManagerWithRealStorageTest() override = default;

  // gtest::TestWithEnvironment:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    auto encryption_service = std::make_unique<encryption::FakeEncryptionService>(dispatcher());
    db_factory_ = std::make_unique<storage::fake::FakeDbFactory>(dispatcher());
    auto ledger_storage = std::make_unique<storage::LedgerStorageImpl>(
        &environment_, encryption_service.get(), db_factory_.get(), DetachedPath(tmpfs_.root_fd()),
        storage::CommitPruningPolicy::NEVER);
    std::unique_ptr<sync_coordinator::FakeLedgerSync> sync =
        std::make_unique<sync_coordinator::FakeLedgerSync>();
    top_level_node_ = inspect_deprecated::Node(kTestTopLevelNodeName.ToString());
    attachment_node_ =
        top_level_node_.CreateChild(kSystemUnderTestAttachmentPointPathComponent.ToString());
    disk_cleanup_manager_ = std::make_unique<FakeDiskCleanupManager>();
    ledger_manager_ = std::make_unique<LedgerManager>(
        &environment_, kLedgerName.ToString(),
        attachment_node_.CreateChild(kInspectPathComponent.ToString()),
        std::move(encryption_service), std::move(ledger_storage), std::move(sync),
        std::vector<PageUsageListener*>{disk_cleanup_manager_.get()});
  }

 protected:
  testing::AssertionResult MutatePage(const PagePtr& page_ptr) {
    page_ptr->Put(convert::ToArray("Hello."),
                  convert::ToArray("Is it me for whom you are looking?"));
    bool sync_callback_called;
    page_ptr->Sync(callback::Capture(callback::SetWhenCalled(&sync_callback_called)));
    RunLoopUntilIdle();
    if (!sync_callback_called) {
      return testing::AssertionFailure() << "Sync callback wasn't called!";
    }
    if (!page_ptr.is_bound()) {
      return testing::AssertionFailure() << "Page pointer came unbound!";
    }
    return testing::AssertionSuccess();
  }

  scoped_tmpfs::ScopedTmpFS tmpfs_;
  std::unique_ptr<storage::fake::FakeDbFactory> db_factory_;
  // TODO(nathaniel): Because we use the ChildrenManager API, we need to do our
  // reads using FIDL, and because we want to use inspect_deprecated::ReadFromFidl for our
  // reads, we need to have these two objects (one parent, one child, both part
  // of the test, and with the system under test attaching to the child) rather
  // than just one. Even though this is test code this is still a layer of
  // indirection that should be eliminable in Inspect's upcoming "VMO-World".
  inspect_deprecated::Node top_level_node_;
  inspect_deprecated::Node attachment_node_;
  std::unique_ptr<FakeDiskCleanupManager> disk_cleanup_manager_;
  std::unique_ptr<LedgerManager> ledger_manager_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerManagerWithRealStorageTest);
};

TEST_F(LedgerManagerWithRealStorageTest, InspectAPIDisconnectedPagePresence) {
  // These PageIds are similar to those seen in use by real components.
  PageId first_page_id = SpecificId("first_page_id___");
  PageId second_page_id = SpecificId("second_page_id__");
  // Real components also use random PageIds.
  PageId third_page_id = RandomId(environment_);
  std::vector<storage::PageId> storage_page_ids({convert::ToString(first_page_id.id),
                                                 convert::ToString(second_page_id.id),
                                                 convert::ToString(third_page_id.id)});
  LedgerPtr ledger_ptr;
  ledger_manager_->BindLedger(ledger_ptr.NewRequest());

  // When nothing has yet requested a page, check that the Inspect hierarchy
  // is as expected with no nodes representing pages.
  inspect_deprecated::ObjectHierarchy zeroth_hierarchy;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &zeroth_hierarchy));
  EXPECT_THAT(zeroth_hierarchy, HierarchyMatcher({}));

  // When one page has been created in the ledger, check that the Inspect
  // hierarchy is as expected with a node for that one page.
  PagePtr first_page_ptr;
  ledger_ptr->GetPage(fidl::MakeOptional(first_page_id), first_page_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy hierarchy_after_one_connection;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_one_connection));
  EXPECT_THAT(hierarchy_after_one_connection, HierarchyMatcher({storage_page_ids[0]}));

  // When two ledgers have been created in the repository, check that the
  // Inspect hierarchy is as expected with nodes for both ledgers.
  PagePtr second_page_ptr;
  ledger_ptr->GetPage(fidl::MakeOptional(second_page_id), second_page_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy hierarchy_after_two_connections;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_two_connections));
  EXPECT_THAT(hierarchy_after_two_connections,
              HierarchyMatcher({storage_page_ids[0], storage_page_ids[1]}));

  // Unbind the first page, but only after having written to it, because it is
  // not guaranteed that the LedgerManager under test will maintain an empty
  // page resident on disk to be described in a later inspection.
  EXPECT_TRUE(MutatePage(first_page_ptr));
  first_page_ptr.Unbind();
  RunLoopUntilIdle();

  // When one of the two pages has been disconnected, check that an inspection
  // still finds both.
  inspect_deprecated::ObjectHierarchy hierarchy_after_one_disconnection;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_one_disconnection));
  EXPECT_THAT(hierarchy_after_one_disconnection,
              HierarchyMatcher({storage_page_ids[0], storage_page_ids[1]}));

  // Check that after a third page connection is made, all three pages appear in
  // an inspection.
  PagePtr third_page_ptr;
  ledger_ptr->GetPage(fidl::MakeOptional(third_page_id), third_page_ptr.NewRequest());
  RunLoopUntilIdle();
  inspect_deprecated::ObjectHierarchy hierarchy_with_second_and_third_connection;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_with_second_and_third_connection));
  EXPECT_THAT(hierarchy_with_second_and_third_connection, HierarchyMatcher(storage_page_ids));

  // Check that after all pages are mutated and unbound all three pages still
  // appear in an inspection.
  EXPECT_TRUE(MutatePage(second_page_ptr));
  second_page_ptr.Unbind();
  EXPECT_TRUE(MutatePage(third_page_ptr));
  third_page_ptr.Unbind();
  RunLoopUntilIdle();

  inspect_deprecated::ObjectHierarchy hierarchy_after_three_disconnections;
  ASSERT_TRUE(Inspect(&top_level_node_, &test_loop(), &hierarchy_after_three_disconnections));
  EXPECT_THAT(hierarchy_after_three_disconnections, HierarchyMatcher(storage_page_ids));
}

}  // namespace
}  // namespace ledger
